#ifndef __event_h_
#define __event_h_

#include <stddef.h>

/*
typedef struct {
  uint64_t time;
  int event_type;
  DO_EVT_FUN *callback_fun;
  struct sendQ_slot *callback_arg;
} Event;
*/


typedef struct __event_queue_type {
    Event *evt;
    uint64_t time;
    struct __event_queue_type *next;
    struct __event_queue_type *prev;
} EventQueue;



#define EVT_TYPE_TIMEOUT(eventtype) ((eventtype).e_timeout)
#define EVT_TYPE_CONGESTION(eventtype) ((eventtype).e_congestion)

#define EVT_TYPE(type) ((type)->event)

#define EVT_TIMEOUT(evt) (EVT_TYPE_TIMEOUT(EVT_TYPE((evt))))
#define EVT_CONGESTION(evt) (EVT_TYPE_CONGESTION(EVT_TYPE((evt))))

#define EVT_ID(evt) ((evt)->id)

#define EVT_IS_TIMEOUT(evt) ((evt)->id == EVENT_ID_TIMEOUT ? 1 : 0)


#define EVT_TIME(evt) ((evt).time)
#define EVT_TIMEVAL(evt) EVT_TIME(EVTQ_EVT((evt)))

#define EVTQ_TIME(eq) ((eq)->time)
#define EVTQ_EVT(eq) ((eq)->evt)

void initializeTimeoutEvent(Event *e);

void initializeEQ(EventQueue * volatile *,EventQueue *volatile *);




Event *evSchedule(DO_EVT_FUN *callback,struct sendQ_slot *send_slot,int EVT_TYPE);
uint64_t evCancel(Event *evt);


void evQueueDelete(EventQueue *L, EventQueue *x );
EventQueue *evQueueSearch(EventQueue *L, Event *k );

EventQueue *forwardIterator(EventQueue*);
EventQueue *backwardIterator(EventQueue*);

DECLARE_DO_TIMER_FUN(proc_timer);

void initializeTimeoutState(TimeoutState *);
uint64_t nextTimeoutValue(TimeoutState*);


void swpTimeout(struct sendQ_slot *slot, int evtType);

void debugEQ(EventQueue *ptr_eq);

#endif
