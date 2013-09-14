#ifndef __libtimer_h_
#define __libtimer_h_

void init_net();
void register_timer(uint64_t interval, void (*handler)(uint64_t));
void register_read(void (*read_fn)(), int fd);
void unregister_timer();
uint64_t get_time();


#endif
