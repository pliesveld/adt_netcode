#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <signal.h>
#include <stdint.h>
#include "libnet.h"

#define TRUE 1
#define FALSE 0


/* Inverse of the system timer. Linux: 10ms, inverse = 100 */
#define RTT_DEVIATION 50 * 1000 /* OTT (one way) deviation over 150 ms */
#define MIN_RTT 150 * 1000 /* Min, one way TT 150 ms */
#define RTT_CHANGE_INTERVAL 1200 * 1000 /* RTT changes over 1200 ms intervals */
#define BW_CHANGE_INTERVAL 3000 * 1000 /* BW changes over 3 sec intervals */
#define MIN_BW 200 * 1000 /* Min BW is 200 Kbps */
#define BW_DEVIATION 100 * 1000 /* BW varies over 200 - 300 Kbps */
#define MAX_DATA_QUEUE 200 /* 200 entry packet queue. Blocks after 200 */
#define TX_LOSS_PROBABILITY 10 /* this is 1% loss probability. however, it drops 4 packets, so it is really 4% */

typedef struct
{
  void *data;
  int len;
  int fd;
  unsigned long long tx_time;
  struct sockaddr to;
  socklen_t addrlen;
} qentry;

static qentry *qptr = NULL;
static volatile int32_t qstart, qend;
static uint64_t last_tx_time;
static uint32_t current_rtt, current_bw;
static uint64_t last_bw_change, last_rtt_change;
static volatile int32_t qlock = 0; /* implements simple semaphore to avoid queue update races */
static unsigned char l_bw= 0, l_rtt= 0, l_rel = 0;
static uint32_t current_packet = 0;
static volatile uint64_t user_timer_interval;
static void (*user_read_fn)() = NULL;
static int32_t user_read_fd = 0;
static void (*user_timer_fn)(uint64_t) = NULL;
static uint64_t  lstart_time;
static FILE *bw_fptr, *rtt_fptr, *drop_fptr;
static unsigned char drop_mode = FALSE;
static unsigned int drop_counter = 0;
static uint32_t drop_train = 0;
static unsigned char is_initialized = FALSE;
static volatile int tlock = 0;

int enqueue_data(int fd, void *data, int len, struct sockaddr *to, socklen_t addrlen);
qentry *dequeue_data(uint64_t current_time);
void timer();

void init_net(unsigned char bw_flag, unsigned char rtt_flag, unsigned char rel_flag,
	      unsigned char random_flag)
{
  struct itimerval itimer;
  
  l_bw = bw_flag;
  l_rtt = rtt_flag;
  l_rel = rel_flag;

  if (is_initialized == TRUE)
    {
      printf("WARNING: libnet has already been initialized.\n");
      printf("WARNING: Ignoring secondary initialization ...\n");
      return;
    }

  is_initialized = TRUE;

  qptr = (qentry *) malloc(MAX_DATA_QUEUE * sizeof(qentry));
  if (!qptr)
    {
      printf("libnet: Unable to allocate internal transmission buffers. Aborting ...\n");
      exit(1);
    }

  qstart = qend = 0;
  if (random_flag) srandom(get_time());
  lstart_time = get_time();
  last_tx_time = lstart_time;

  bw_fptr = fopen("bw.txt", "w+");
  drop_fptr = fopen("drop.txt", "w+");
  rtt_fptr = fopen("rtt.txt", "w+");
  if (!bw_fptr || !drop_fptr || !rtt_fptr)
    {
      printf("Unable to open result graph files. Aborting ...\n");
      exit(1);
    }

  current_rtt = MIN_RTT;
  current_bw = MIN_BW;
  fprintf(bw_fptr, "0 %u\n", current_bw);
  fprintf(rtt_fptr, "0 %u\n", current_rtt/1000);
  
  signal(SIGALRM, timer);
  itimer.it_interval.tv_sec = 0;
  itimer.it_interval.tv_usec = 1000; /* 1 ms timer */
  //itimer.it_interval.tv_usec = 50000; /* 1 ms timer */
  itimer.it_value.tv_sec = 0;
  itimer.it_value.tv_usec = 1000;
  setitimer(ITIMER_REAL, &itimer, NULL);

  fflush(bw_fptr);
  fflush(rtt_fptr);
  fflush(drop_fptr);
  
}

void close_net()
{
  qentry *ptr;
  volatile unsigned long long current_time;
  struct itimerval itimer;

  if (is_initialized == FALSE)
    {
      printf("libnet has not been initialized.\n");
      printf("libnet functions should not be called before initialization\n");
      exit(-1);
    }

   /* Disable timer */
  user_read_fn = NULL;
  itimer.it_interval.tv_sec = 0;
  itimer.it_interval.tv_usec = 0; 
  itimer.it_value.tv_sec = 0;
  itimer.it_value.tv_usec = 0;
  setitimer(ITIMER_REAL, &itimer, NULL);
  signal(SIGALRM, SIG_IGN);

  /* empty any remaining queue entries */
  while (1)
    { 
      if (qstart == qend) 
	{
	  printf("breaking out since head == tail\n");
	  break;
	}
      current_time = get_time();
      ptr = dequeue_data(current_time);
      if (ptr ==  NULL) continue; /* Maybe time hasn't passed yet */
      sendto(ptr->fd, ptr->data, ptr->len, 0, &(ptr->to), ptr->addrlen);
      free(ptr->data);
    } 

  
  fclose(bw_fptr);
  fclose(rtt_fptr);
  fclose(drop_fptr);

  is_initialized = FALSE;
}


void register_timer(uint64_t interval, void (*handler)(uint64_t))
{
  if (is_initialized == FALSE)
    {
      printf("libnet has not been initialized.\n");
      printf("libnet functions should not be called before initialization\n");
      exit(-1);
    }

  /* make sure we are not in the timer handler before setting this */
  // while (tlock) ;

  user_timer_fn = handler;
  user_timer_interval = interval;
}


void register_read(void (*read_fn)(), int fd)
{
   if (is_initialized == FALSE)
    {
      printf("libnet has not been initialized.\n");
      printf("libnet functions should not be called before initialization\n");
      exit(-1);
    }

  printf("Read fn registered. Registering FD %d\n", fd);
  user_read_fn = read_fn;
  user_read_fd = fd;
}
  
void unregister_timer()
{
  if (is_initialized == FALSE)
    {
      printf("libnet has not been initialized.\n");
      printf("libnet functions should not be called before initialization\n");
      exit(-1);
    }
  user_timer_fn = NULL;
}


int u_sendto(int fd, void *data, int len, int flags, struct sockaddr *to, 
	     socklen_t addrlen)
{
  if (is_initialized == FALSE)
    {
      printf("libnet has not been initialized.\n");
      printf("libnet functions should not be called before initialization\n");
      exit(-1);
    }
  if (len < 0 || data == NULL) return(BAD_DATA);
  if (len == 0) return(0);
  if (len > MTU) return(BAD_LENGTH);

  enqueue_data(fd, data, len, to, addrlen);
  return(len);
}

int enqueue_data(int fd, void *data, int len, struct sockaddr *to, socklen_t addrlen)
{
  int p_loss;
  int drop_len;
  unsigned long long current_time;

  if (is_initialized == FALSE)
    {
      printf("libnet has not been initialized.\n");
      printf("libnet functions should not be called before initialization\n");
      exit(-1);
    }

  /* Block on data transmission if the queues are full.
     The timer will empty them */

  while (((qend + 1) % MAX_DATA_QUEUE) == qstart) ; 
  
  qlock = 1;

  current_packet++;
  
  current_time = get_time();
  if (l_rel)
    {
      p_loss = random() % 1000;
      if ((p_loss < TX_LOSS_PROBABILITY) || (drop_mode == TRUE))
	{
	  if (drop_mode == FALSE)
	    drop_train = (random() % 4) + 2; /* mean of 4 packets being dropped */

	  drop_mode = TRUE;
	  /* Transmit part of the packet before dropping it */
	  fprintf(drop_fptr, "%llu %u\n", (current_time-lstart_time)/1000, 
		  current_packet);
	  if (current_time > last_tx_time) last_tx_time = current_time;
	  drop_len = len;
	  last_tx_time += 
	    (drop_len*8*(int)((double)(1000000.0/(double)current_bw)));
	  
	  /* Block while we transmit the packet */
	  while (get_time() < last_tx_time) ;

	  fflush(drop_fptr);
	  drop_counter ++;
	  if (drop_counter >= drop_train)
	    {
	      drop_mode = FALSE;
	      drop_counter = 0;
	      drop_train = 0;
	    }
	  qlock = 0;
	  timer(); /* call the timer to get any pending user timer calls */
	  return(1);
	}
    }
  
  qptr[qend].data = (unsigned char *) malloc(len);
  if (!qptr[qend].data)
    {
      printf("libnet: Unable to allocate internal transmission buffers.\n");
      fprintf(drop_fptr, "%llu %u\n", (current_time-lstart_time)/1000, 
	      current_packet);
      if (current_time > last_tx_time) last_tx_time = current_time;
      drop_len = len;
      last_tx_time += 
	(drop_len*8*(int)((double)(1000000.0/(double)current_bw)));
      fflush(drop_fptr);
      qlock = 0;
      timer();
      return(1);
    }

  memcpy(qptr[qend].data, data, len);
  qptr[qend].len = len;
  qptr[qend].fd = fd;
  qptr[qend].to = *(struct sockaddr *)to;
  qptr[qend].addrlen = addrlen;
  
  
  /* Change data rate to bandwidth bottleneck rate */
  if (current_time > last_tx_time) last_tx_time = current_time;
  last_tx_time += (len*8*(int)((double)(1000000.0/(double)current_bw)));

  /* Block while we transmit the packet */
  while (get_time() < last_tx_time) ;
  
  /* Now queue it to add RTT effect */
  qptr[qend].tx_time = get_time() + current_rtt;

  qend = ((qend + 1) % MAX_DATA_QUEUE);

  qlock = 0;

  timer();
  return(1);
}

qentry *dequeue_data(uint64_t current_time)
{
  qentry *ptr;

  if (qstart == qend) return(NULL); /* Empty queue */

  if (current_time >= qptr[qstart].tx_time) 
    {
      ptr = &(qptr[qstart]);
      qstart = (qstart + 1) % MAX_DATA_QUEUE;
      return(ptr);
    }
  else return(NULL);
}

void timer()
{
  uint64_t current_time;
  qentry *ptr;
  

  if (is_initialized == FALSE)
    {
      printf("libnet has not been initialized.\n");
      printf("libnet functions should not be called before initialization\n");
      exit(-1);
    }

  if (tlock) return;
  tlock = 1;
  
  /* qlock is immaterial here. We should be able to do a real 
     phy transmit even if u_sendto is blocked, else we'll have RTT
     errors */
  while (1)
    {
      current_time = get_time();
      ptr = dequeue_data(current_time);
      if (ptr ==  NULL) break;
      sendto(ptr->fd, ptr->data, ptr->len, 0, &(ptr->to), ptr->addrlen);
      free(ptr->data);
    }

  if (l_rtt && current_time - last_rtt_change > RTT_CHANGE_INTERVAL)
    {
      current_rtt = (random() % RTT_DEVIATION) + MIN_RTT;
      last_rtt_change = current_time;
      fprintf(rtt_fptr, "%llu %u\n", (unsigned long long)((current_time-lstart_time)/1000), 
	      current_rtt/1000);
      fflush(rtt_fptr);
    }

  if (l_bw && current_time - last_bw_change > BW_CHANGE_INTERVAL)
    {
      current_bw = (random() % BW_DEVIATION) + MIN_BW;
      last_bw_change = current_time;
      fprintf(bw_fptr, "%llu %u\n", (unsigned long long) ((current_time-lstart_time)/1000),
	      current_bw);
      fflush(bw_fptr);
    }

  /* Blocked in transmit call. Don't call user
     timer since it may try to transmit and cause
     a race. Similar justification for not 
     calling user read fn. It may transmit and
     cause a race */
  if (qlock) 
    { 
      tlock = 0; 
      return; 
    }

  if ((user_timer_interval > 0) && (user_timer_fn != NULL) && (current_time >= user_timer_interval))
    {
      uint64_t last_timer = user_timer_interval;
      (*user_timer_fn)(current_time);
      if (user_timer_interval == last_timer) user_timer_fn = NULL;
    }

  if ((user_timer_interval > 0) && (user_read_fn != NULL))
    {
      fd_set rfds;
      struct timeval tv;
      int retval;

      while (1)
	{
	  FD_ZERO(&rfds);
	  FD_SET(user_read_fd, &rfds);

	  /* Don't wait. */
	  tv.tv_sec = 0;
	  tv.tv_usec = 0;
	  
	  retval = select(user_read_fd+1, &rfds, NULL, NULL, &tv);
	  if (retval) (*user_read_fn)();
	  else break;
	}
    }
  tlock = 0;
}

uint64_t get_time()
{
  struct timeval time_val;
  struct timezone time_zone;
  static uint64_t last_time = 0ll;
  uint64_t current_time;

  if (is_initialized == FALSE)
    {
      printf("libnet has not been initialized.\n");
      printf("libnet functions should not be called before initialization\n");
      exit(-1);
    }

  current_time = 0ll;
  gettimeofday(&time_val, &time_zone);
  current_time = (uint64_t) time_val.tv_sec * 1000000ULL + 
    (uint64_t) time_val.tv_usec;
  
  if (current_time < last_time) return(last_time);

  last_time = current_time;
  return(current_time);
}


    
  
  
  
