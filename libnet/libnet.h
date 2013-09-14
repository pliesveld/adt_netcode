#ifndef __LIBNET_H
#define __LIBNET_H

#include <stdint.h>

/* Some error codes returned by u_sendto */
#define BAD_DATA -1
#define BAD_LENGTH -2
#define BUFFER_OVERFLOW -3

/* Maximum length of a data packet in a single call to u_sendto */
#define MTU 500

/* 
   flags should be 0 or non-zero
   
   bw_flag: 0 = constant bottleneck bw, 1 = variable bottleneck bw
   rtt_flag: 0 = const rtt, 1 = variable rtt
   rel_flag: 0 = tx are reliable, 1 = tx is unreliable
   random_flag: 0 = same random numbers on each run
                1 = random numbers change on each run
*/
void init_net(unsigned char bw_flag, unsigned char rtt_flag, unsigned char rel_flag,
	      unsigned char random_flag);
void close_net();

/* 
   Timer functions
   Interval is a 64 bit number.
   Note: System only provides 1 timer. You need to implement your own linked
   list to use multiple timers.

   The minimum timer resolution is 10ms. interval (in us) is the time at which your
   function will be called. 

   The timer function is a one shot. After the timer expires, your function is called
   and the timer is deleted. To create another timer, call the register timer
   function again.
*/
void register_timer(uint64_t interval, void (*handler)(uint64_t));
void unregister_timer();

/* 
   Register a read function at the server. This function is called asynchronously if 
   any data is received. You need to pass the socket file handle of the open UDP socket
*/
void register_read(void (*read_fn)(), int fd);


/* Utility Function: Returns current time in us
   Note: Function returns a 64 bit number  */
uint64_t get_time();


/* Write function. Same parameters as UDP sendto */
int u_sendto(int fd, void *data, int len, int flags, struct sockaddr *to,
	     socklen_t addrlen);

#endif
