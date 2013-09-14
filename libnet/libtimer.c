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

#include "libtimer.h"

typedef struct
{
  void *data;
  int len;
  int fd;
  unsigned long long tx_time;
  struct sockaddr to;
  socklen_t addrlen;
} qentry;

static volatile int32_t qlock = 0; /* implements simple semaphore to avoid queue update races */

static volatile uint64_t user_timer_interval;
static void (*user_read_fn)() = NULL;
static int32_t user_read_fd = 0;
static void (*user_timer_fn)(uint64_t) = NULL;
static volatile int tlock = 0;

void timer();

void init_net()
{
  struct itimerval itimer;
  
  signal(SIGALRM, timer);
  itimer.it_interval.tv_sec = 0;
  //itimer.it_interval.tv_usec = 1000; /* 1 ms timer */
  itimer.it_interval.tv_usec = 500000; /* 500 ms timer */
  itimer.it_value.tv_sec = 0;
  itimer.it_value.tv_usec = 1000;
  setitimer(ITIMER_REAL, &itimer, NULL);
}

void register_timer(uint64_t interval, void (*handler)(uint64_t))
{
  user_timer_fn = handler;
  user_timer_interval = interval;
}


void register_read(void (*read_fn)(), int fd)
{
  user_read_fn = read_fn;
  user_read_fd = fd;
}
  
void unregister_timer()
{
 user_timer_fn = NULL;
}



void timer()
{
  uint64_t current_time = get_time();
  

  if (tlock) return;
  tlock = 1;
  
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

  current_time = 0ll;
  gettimeofday(&time_val, &time_zone);
  current_time = (uint64_t) time_val.tv_sec * 1000000ULL + 
    (uint64_t) time_val.tv_usec;
  
  if (current_time < last_time) return(last_time);

  last_time = current_time;
  return(current_time);
}

