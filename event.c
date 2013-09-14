
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>

#include <math.h>
#include <semaphore.h>
#include <linux/sem.h>


#include <libnet.h>

#include "msg.h"
#include "transport.h"
#include "swp.h"
#include "event.h"
#include "xfer.h"
#include "comm.h"

static EventQueue EQ_Tail = { (void *) ((int) 0), 0, &EQ_Tail, &EQ_Tail} ;
static EventQueue EQ_Head = { (void *) ((int) 0), 0, &EQ_Head, &EQ_Head} ;

typedef int DO_QUE_CMP_FUN args((EventQueue *lhs, EventQueue *rhs));
typedef EventQueue *DO_ITER_FUN args((EventQueue *iter));


static void evQueueInsert(EventQueue *L, EventQueue * x);
static EventQueue *evScanUntil(EventQueue *L,
                               EventQueue *x,
                               DO_QUE_CMP_FUN *compare_fun,
                               int *cmp__and_ret_value,
                               DO_ITER_FUN *traversal_fun);

static EventQueue *evScanWhile(EventQueue *L,
                               EventQueue *x,
                               DO_QUE_CMP_FUN *compare_fun,
                               int *cmp_value,
                               DO_ITER_FUN *traversal_fun);

static int compareByTime(EventQueue *lhs, EventQueue *rhs);
static int compareByEvtTime(EventQueue *lhs, EventQueue *rhs);
static int compareByEvtPriority(EventQueue *lhs, EventQueue *rhs);
static int compareByEvtType(EventQueue *lhs, EventQueue *rhs);


static void queue_evt(Event *);
static uint64_t dequeue_evt(EventQueue *,Event *);


static void recordSampleRTT(TimeoutState *, uint64_t);


static Event *allocEvent();

static const char *evtType_to_str(int);

void swpTimeout(struct sendQ_slot *slot, int evtType)
{

    dbprintf(stderr,1,DB_PRINT_ALWAYS,"event %s occured on slot %d\n", evtType_to_str(evtType), slot->i_sendq);
    switch(evtType) {
    case SWP_SEND_TIMEOUT_ON_TIMEOUT:
        *slot->timeoutInterval *= 2;
        slot->SACKed = 0;
    case SWP_SEND_TIMEOUT:
        *slot->timeoutInterval = UMIN(*slot->timeoutInterval, 3000000ULL );
        EVT_TIME(EVT_TIMEOUT(slot->timeout)) = *slot->timeoutInterval;
        recordRTT(EVT_TIME(EVT_TIMEOUT(slot->timeout)));
        slot->timeout = evSchedule(&swpTimeout,slot,SWP_SEND_TIMEOUT_ON_TIMEOUT);
        if(!slot->SACKed)
        {
            slot->SACKed = 0;
            sendLINK(&slot->msg);
        }
        break;
        // Reset CongWin
    case SWP_SEND_FASTRETRANSMIT:
        //evSchedule(&swpFastRecovery,slot,SWP_SEND_FASTRECOVERY);
        //sendLINK(&slot->msg);

        break;


    default:
        exit(1);
        break;

    };

    //register_timer(EQ_Head.time, &proc_timer);
}

EventQueue *allocEQ();


static void evQueueInsert(EventQueue *L, EventQueue * x )
{
    x->next = L->next;
    L->next->prev = x;
    L->next = x;
    x->prev = L;
}

void evQueueDelete(EventQueue *L, EventQueue *x )
{
    EventQueue *tmp = L->next;
    for(; tmp != NULL; tmp = tmp->next)
    {
        if(tmp == x)
        {
            x->prev->next = x->next;
            x->next->prev = x->prev;
            return;
        }
    }
}

EventQueue *evQueueSearch(EventQueue *L, Event *k)
{
    EventQueue *x = L->next;
    while( x != L && EVTQ_EVT(x) != k )
    {
        x = x->next;
    }
    return x;
}

static int compareByTime(EventQueue *lhs, EventQueue *rhs)
{
    if( lhs == rhs )
        return -1;
    return ( lhs->time > rhs->time );
}

static int compareByEvtTime(EventQueue *lhs, EventQueue *rhs)
{
    if( EVT_ID( EVTQ_EVT(lhs) ) !=
            EVT_ID( EVTQ_EVT(rhs) ) )
        return -1;

    switch(EVT_ID(EVTQ_EVT(lhs)))
    {
    case EVENT_ID_TIMEOUT:
        return EVT_TIME(EVT_TIMEOUT(EVTQ_EVT(lhs)))
               > EVT_TIME(EVT_TIMEOUT(EVTQ_EVT(rhs)));
        break;

    case EVENT_ID_CONGESTION:
        return EVT_TIME(EVT_CONGESTION(EVTQ_EVT(lhs)))
               > EVT_TIME(EVT_CONGESTION(EVTQ_EVT(rhs)));
        break;
    default:
        exit(1);
        break;
    }
    return -1;
}


static int compareByEvtPriority(EventQueue *lhs, EventQueue *rhs)
{
    if( lhs == &EQ_Head || lhs == &EQ_Tail ||
            rhs == &EQ_Head || rhs == &EQ_Tail || EVT_ID( EVTQ_EVT(lhs) ) !=
            EVT_ID( EVTQ_EVT(rhs) ) || ( EVT_IS_TIMEOUT(EVTQ_EVT(lhs)) && EVT_TIMEOUT(EVTQ_EVT(lhs)).event_type == EVT_TIMEOUT(EVTQ_EVT(rhs)).event_type) )
        return -1;

    return EVT_TIMEOUT(EVTQ_EVT(lhs)).event_type > EVT_TIMEOUT(EVTQ_EVT(rhs)).event_type;
}
int compareByEvtType(EventQueue *lhs, EventQueue *rhs)
{
    if( lhs == &EQ_Head || lhs == &EQ_Tail ||
            rhs == &EQ_Head || rhs == &EQ_Tail )
        return -1;

    return ( EVT_ID( EVTQ_EVT(lhs) ) == EVT_ID( EVTQ_EVT(rhs) )
             && ( EVT_IS_TIMEOUT(EVTQ_EVT(lhs))
                  && EVT_TIMEOUT(EVTQ_EVT(lhs)).event_type == EVT_TIMEOUT(EVTQ_EVT(rhs)).event_type) );

}



static EventQueue *evScanUntil(EventQueue *L,
                               EventQueue *x,
                               DO_QUE_CMP_FUN *compare_fun,
                               int *cmp_value,
                               DO_ITER_FUN *traversal_fun)
{
    EventQueue *x_ptr;
    EventQueue *x_next;
    int condition = *cmp_value;

    for( x_ptr = L, x_next = (*traversal_fun)(x_ptr); x_next != L; x_ptr = x_next)
    {
        x_next = (*traversal_fun)(x_ptr);
        if( x_next == L ) return NULL;
        if( (*cmp_value = (*compare_fun)(x_ptr, x)) == condition )
            break;
    }

    return x_ptr;
}


static EventQueue *evScanWhile(EventQueue *L,
                               EventQueue *x,
                               DO_QUE_CMP_FUN *compare_fun,
                               int *cmp_value,
                               DO_ITER_FUN *traversal_fun)
{
    EventQueue *x_ptr;
    EventQueue *x_next;
    int condition = *cmp_value;

    for( x_ptr = L, x_next = (*traversal_fun)(x_ptr); x_next != L; x_ptr = x_next)
    {
        x_next = (*traversal_fun)(x_ptr);

        if( x_next == L ) return NULL;
        if( condition != (*cmp_value = (*compare_fun)(x_ptr, x)) )
            break;
    }

    return x_ptr;
}


EventQueue *forwardIterator(EventQueue* x)
{
    return x->next;
}

EventQueue *backwardIterator(EventQueue* x)
{
    return x->prev;
}


static void queue_evt(Event *e)
{
    EventQueue * eq_e = NULL;
    EventQueue * volatile eq_ptr = NULL;
    uint64_t current_time;
    EventQueue *p_next  = NULL;

    eq_e = (EventQueue *) malloc(sizeof(EventQueue));
    eq_e->evt = NULL;
    eq_e->time = (uint64_t)0;
    eq_e->next = NULL;
    eq_e->prev = NULL;

    eq_e->evt = e;

    eq_ptr = &EQ_Head;
    EVTQ_TIME(eq_e) = (current_time = get_time()) + EVT_TIME(EVT_TIMEOUT(e));

    if( eq_ptr->prev == eq_ptr )
    {
        evQueueInsert(eq_ptr, eq_e);
        eq_ptr->evt = (void *) ((int)1);
        eq_ptr->time = eq_e->time;
        return;
    }


    for (
        eq_ptr = &EQ_Head,
        eq_ptr = forwardIterator(eq_ptr),
        p_next = forwardIterator(eq_ptr) ;
        ( eq_ptr != &EQ_Head ) ; eq_ptr = p_next )
    {
        p_next = forwardIterator(eq_ptr);


        if( compareByEvtPriority( eq_ptr, eq_e) == 0)
            continue;


        if( compareByEvtType(eq_ptr, eq_e) )
            continue;

        break;

    }

    evQueueInsert(eq_ptr, eq_e);
    EQ_Head.evt = (void *) ((intptr_t)EQ_Head.evt + 1);
    EQ_Head.time = EVTQ_TIME(EQ_Head.prev);
}

static uint64_t dequeue_evt(EventQueue *L, Event *e)
{
    EventQueue *eq_ptr = NULL;
    uint64_t eventTime = 0ULL;
    uint64_t nextTime = 0ULL;

    eq_ptr = evQueueSearch(L, e);

    if( eq_ptr == L)
        return 0ULL;

    if(EVTQ_EVT(L->prev) == e)
    {
        nextTime = eq_ptr->prev->time;
    } else {
        nextTime = eq_ptr->time;
    }

    evQueueDelete(L,eq_ptr);
    L->evt = (void *) ((intptr_t) L->evt - 1);
    eventTime = EVTQ_TIME(eq_ptr);

    if(L->next != L)
        L->time = nextTime;
    else
        L->time = (uint64_t) 0ULL;
    free(eq_ptr);

    return eventTime;
}

/*
static Event* findEvtByTime(const uint64_t *t)
{
  EventQueue volatile * ptr = &EQ_Head;
  EventQueue *ptr_next = EQ_Head.next;
  Event *e = NULL;

  for(; ptr_next != &EQ_Tail; ptr = ptr_next)
  {
    ptr_next = ptr->next;

    if(EVTQ_TIME(ptr_next) <= *t)
    {
	ptr->next = ptr_next->next;
	ptr_next->next = NULL;
        e = ptr_next->evt;
        EQ_Head.evt = (void *) ((int)EQ_Head.evt - 1);
	free(ptr_next);
	return e;
    }
  }
  return e;
}
*/
/* Caller is responsible for deallocating */
static EventQueue* findEvtQueByTime(uint64_t t)
{
    EventQueue * volatile ptr = &EQ_Head;
    EventQueue *ptr_next = NULL;

    ptr_next = ptr->next;

    if(ptr_next == ptr) {
        EQ_Head.evt = (void *) ((int)0);
        EQ_Head.time = (uint64_t)0;

        return NULL;
    }

    if( t )
    {
        EQ_Head.time = EVTQ_TIME(ptr_next->prev);
        evQueueDelete(&EQ_Head,ptr_next);
        EQ_Head.evt = (void *) ((intptr_t)EQ_Head.evt - 1);
        if( EQ_Head.prev == &EQ_Head )
        {
            EQ_Head.time  = 0;
        }
    } else {
        ptr_next = NULL;
    }

    if( (intptr_t)EQ_Head.evt == 0 || EQ_Head.next == &EQ_Head)
    {
        if( (intptr_t)EQ_Head.evt != 0 || EQ_Head.prev != &EQ_Head )
        {
            dbprintf(stderr,0,DB_PRINT_ALWAYS,"failed EQ_Head sanity check\n");
            exit(1);
        }
        EQ_Head.evt = (void *) ((int)0);
        EQ_Head.time = (uint64_t)0;
    }


    return ptr_next;
}



Event *evSchedule(DO_EVT_FUN *callback,
                  struct sendQ_slot *slot,
                  int etype)
{
    Event *event;

    register_timer(0,NULL);
    event = slot->timeout;

    switch (etype)
    {
    case SWP_SEND_FASTRETRANSMIT:
        (*EVT_TIMEOUT(event).callback_fun)(EVT_TIMEOUT(event).callback_arg,SWP_SEND_FASTRETRANSMIT);
        return event;
        break;
    case SWP_SEND_TIMEOUT:
    case SWP_SEND_TIMEOUT_ON_TIMEOUT:
        EVT_ID(event) = EVENT_ID_TIMEOUT;

    default:
        EVT_TIMEOUT(event).event_type = etype;
        EVT_TIMEOUT(event).callback_arg = slot;
        EVT_TIMEOUT(event).callback_fun = callback;

        break;
    }

    queue_evt(event);

    return event;
}


static int debugPrintTimeDiff(char * const buf, uint16_t len, uint64_t *t1, uint64_t *t2)
{
    char tbuffer[16];
    uint32_t seconds;
    uint32_t miliseconds;
    uint32_t microseconds;
    int expired = 0;
    uint64_t *lhs = t1;
    uint64_t *rhs = t2;

    bzero(buf,len);
    strcpy(buf,"");

    if(*t2 > *t1)
    {
        lhs = t2;
        rhs = t1;
        expired = 1;
    }


    seconds = ((uint64_t) *lhs - *rhs)/1000000;
    miliseconds = ((((uint64_t) *lhs - *rhs)/1000)%1000);
    microseconds = ((uint64_t) *lhs - *rhs)%1000;

    sprintf(tbuffer," %u sec%s", seconds, seconds > 1 ? "s" : "");
    if( seconds > 0 )
        strcat(buf,tbuffer);
    sprintf(tbuffer," %u msec%s", miliseconds, miliseconds > 1 ? "s" : "" );
    if( miliseconds > 0 )
        strcat(buf,tbuffer);
    sprintf(tbuffer," %u usec%s", microseconds, microseconds > 1 ? "s" : "");
    if( microseconds > 0 )
        strcat(buf,tbuffer);

    return expired;
}

uint64_t evCancel(Event *evt)
{
    uint64_t e_time = 0ULL;
    uint64_t to_time = 0ULL;
    EventQueue *Sent = NULL;
    register_timer(0,NULL);
    unregister_timer();

    if( (e_time = dequeue_evt(&EQ_Tail,evt)) > 0 ) {
        to_time = e_time - EVT_TIMEOUT(evt).time;
        Sent = &EQ_Tail;
    } else if( (e_time = dequeue_evt(&EQ_Head,evt)) > 0)
    {
        to_time = e_time - EVT_TIMEOUT(evt).time;
        Sent = &EQ_Head;
    }
    if(Sent == NULL)
    {
        dbprintf(stderr,0,DB_PRINT_ALWAYS,"no event %p to cancel\n", evt);
        exit(4);
    }

    register_timers();
    return to_time;
}




void debugEQ(EventQueue *p_eq)
{
    char buffer[1024];
    char diffbuffer[48];
    EventQueue *ptr = NULL;
    EventQueue *ptr_next = NULL;
    int i = 0;
    EventQueue *ptr_eq = NULL;

    if(p_eq == NULL)
        ptr_eq = &EQ_Head;
    else
        ptr_eq = p_eq;

    bzero(buffer,sizeof(buffer));
    bzero(diffbuffer,sizeof(diffbuffer));


    ptr = ptr_eq->next;
    ptr_next = ptr->next;

    debugPrintTimeDiff(diffbuffer,sizeof(diffbuffer),&EVTQ_TIME(ptr),  &EVTQ_TIME(ptr_eq));

    dbprintf(stderr,2,DB_PRINT_ALWAYS,"EvetQueue; #of entries %d Head Time %llu #of Tail entries %d Tail Time %llu (diff %s)\n",
             (intptr_t)EQ_Head.evt, EQ_Head.time, (intptr_t)EQ_Tail.evt ,EQ_Tail.time, diffbuffer);


    for( ; ptr_eq != ptr; ptr = ptr_next )
    {
        uint64_t * lhs =  &EVTQ_TIME(ptr);
        //uint64_t * rhs =  &EVTQ_TIME((EventQueue * volatile)ptr_eq);
        uint64_t * rhs =  &EVTQ_TIME(ptr_eq);
        int expired = 0;
        SwpHdr *p_hdr;

        ptr_next = ptr->next;

        bzero(diffbuffer,sizeof(diffbuffer));
        expired = debugPrintTimeDiff(diffbuffer,sizeof(diffbuffer),lhs,rhs);
        if(!EVTQ_EVT(ptr) || ptr == &EQ_Head || ptr == &EQ_Tail)
            break;

        if(!EVT_IS_TIMEOUT(EVTQ_EVT(ptr)))
            break;

        p_hdr = (SwpHdr*)&EVT_TIMEOUT(EVTQ_EVT(ptr)).callback_arg->msg.buffer;
        sprintf(buffer, "%s in slot %d (seq %u%s) timesout%s%s%s",
                !EVT_IS_TIMEOUT(EVTQ_EVT(ptr)) ? "NULL" : evtType_to_str(EVT_TIMEOUT(EVTQ_EVT(ptr)).event_type),
                (!EVT_IS_TIMEOUT(EVTQ_EVT(ptr)) || !EVT_TIMEOUT(EVTQ_EVT(ptr)).callback_arg) ? -1 : EVT_TIMEOUT(EVTQ_EVT(ptr)).callback_arg->i_sendq,
                p_hdr->SeqNum,
                EVT_TIMEOUT(EVTQ_EVT(ptr)).callback_arg->SACKed == 0 ? "" : " SACKed",
                expired ? " expired" : "s in",
                diffbuffer,
                expired ? " ago" : "");
        //!EVTQ_EVT(ptr) ? -1 : EVT_TIMEVAL(ptr));

        dbprintf(stderr,1,DB_PRINT_ALWAYS, "eq #%d; evt %p at t%-16llu %s\n",++i, ptr->evt ? ptr->evt : 0, ptr->time, buffer);
        ptr = ptr->next;
    }

}

uint64_t nextTimeoutValue(TimeoutState* t)
{
    int64_t diff;
    t->estimatedRTT = (uint64_t) ceil(ceil((0.875000*(double)t->estimatedRTT)) + ceil((((double)t->sampleRTT/1000.00000)*0.125000)));
    //t->estimatedRTT = UMIN( t->estimatedRTT , 3000ULL );
    diff = (int64_t) ceil((round((double)t->estimatedRTT*1.000000) - round((double)t->sampleRTT/1000.00000)));

    if( diff < 0)
        diff =- diff;

    t->estimatedRTTdev = (uint64_t)(ceil((0.750000*(double)t->estimatedRTTdev*1.0000)) + ceil((0.250000*(double)diff*1.0000)));
    t->timeout = t->estimatedRTT + 4*t->estimatedRTTdev;

    t->timeout *= 1000;
    //t->timeout = UMIN(t->timeout, 3000000);
    return t->timeout;

}

void proc_timer(uint64_t time)
{
    char buf[48];
    EventQueue * volatile p_evtQ;
    int i = 0, j = 0;
    dbprintf(stderr,2,DB_PRINT_ALWAYS, "eventhandler callback at time %llu\n",time);
    debugEQ(NULL);
    while( (p_evtQ = findEvtQueByTime(time)) != NULL )
    {
        Event *evt = EVTQ_EVT(p_evtQ);

        debugPrintTimeDiff(buf,sizeof(buf), &EVTQ_TIME(p_evtQ), &time);
        dbprintf(stderr,2,DB_PRINT_ALWAYS, "Timeout event #%d at time (sched) %llu (actu) %llu (diff %s) used a to value %llu\n",
                 ++i, EVTQ_TIME(p_evtQ), time, buf, EVT_TIMEOUT(evt).time);
        ++j;

        evQueueInsert(EQ_Tail.prev,p_evtQ);

    }

    if( EQ_Tail.prev != &EQ_Tail ) {
        EQ_Tail.time = EVTQ_TIME(EQ_Tail.prev);
        EQ_Tail.evt = (void *)((intptr_t)EQ_Tail.evt + j);
        dbprintf(stderr,1,DB_PRINT_ALWAYS, "Total of %d timeoutevents.  Number of callbacks %d.\n",(intptr_t)EQ_Head.evt,j );
        congestion_event(time);
    } else {
        EQ_Tail.time = 0;
        EQ_Tail.evt = (void *)((int)0);
    }


}

void initializeTimeoutState(TimeoutState *e_state)
{
    e_state->sampleMean = 0;
    e_state->sampleVar = 0;
    e_state->n_samples = 0;
    e_state->sampleRTT = 0;
    e_state->estimatedRTT= 0;
    e_state->estimatedRTTdev = 0;
    e_state->timeout = 3000000ULL;
}

static void recordSampleRTT(TimeoutState *t_state, uint64_t sampleRTT)
{
    uint64_t prev_sample = t_state->sampleMean;
    int j = ++t_state->n_samples;
    uint64_t sample_mean = t_state->sampleMean;
    uint64_t sample_var = t_state->sampleVar;

    switch(j)
    {
    case 0:
        sample_mean = 0;
        sample_var = 0;
        break;
    case 1:
        sample_mean = sampleRTT;
        sample_var = 0;
        break;
    default:
        sample_mean = sample_mean + ((sampleRTT - sample_mean)/j);
        sample_var = (1 - (1/(j - 1)))*sample_var + t_state->n_samples*pow(sample_mean - prev_sample,2);
    };

    t_state->sampleMean = sample_mean;
    t_state->sampleVar = (uint64_t) sqrtl((long double)sample_var);

}

void updateRTTSampleMean(TimeoutState *t_state, uint64_t time)
{
    uint64_t t_diff = get_time() - time;
    recordSampleRTT(t_state, t_diff);
}


void initializeTimeoutEvent(Event *e)
{
    EVT_ID(e) = EVENT_ID_TIMEOUT;
    EVT_TIMEOUT(e).time = 0;
    EVT_TIMEOUT(e).event_type = 0;
    EVT_TIMEOUT(e).callback_fun = NULL;
    EVT_TIMEOUT(e).callback_arg = NULL;
}

void initializeCongestionEvent(Event *e)
{
    EVT_ID(e) = EVENT_ID_CONGESTION;
    EVT_CONGESTION(e).time = 0;
    EVT_CONGESTION(e).n_packets = 0;
    EVT_CONGESTION(e).callback_fun = NULL;
    EVT_CONGESTION(e).callback_arg = NULL;
}


static Event *allocEvent(int id)
{
    Event *e = (Event *) malloc(sizeof(struct event_type));
    switch (id)
    {
    case EVENT_ID_TIMEOUT:
        initializeTimeoutEvent(e);
        break;
    case EVENT_ID_CONGESTION:
        initializeCongestionEvent(e);
        break;
    default:
        break;
    };
    return e;
}

EventQueue *allocEQ()
{
    EventQueue * eq_i = (EventQueue *) malloc(sizeof(EventQueue));
    eq_i->evt = NULL;
    eq_i->time = (uint64_t)0;
    eq_i->next = NULL;
    eq_i->prev = NULL;
    return eq_i;
}

void initializeEQ(EventQueue *volatile * hp, EventQueue *volatile * tp)
{
    *hp = &EQ_Head;
    *tp = &EQ_Tail;
}

static const char *evtType_to_str(int type)
{
    switch(type)
    {
    case SWP_SEND_TIMEOUT:
        return "SWP_SEND_TIMEOUT";
        break;
    case SWP_SEND_TIMEOUT_ON_TIMEOUT:
        return "SWP_SEND_TIMEOUT_ON_TIMEOUT";
        break;
    case SWP_SEND_FASTRETRANSMIT:
        return "SWP_SEND_FASTRETRANSMIT";
        break;
    case SWP_SEND_FASTRECOVERY:
        return "SWP_SEND_FASTRECOVERY";
        break;
    default:
        return "UNKOWN EVENT TYPE";
        break;
    };

}
