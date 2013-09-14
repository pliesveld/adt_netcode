#ifndef __transport_h
#define __transport_h
#include "transport_meta.h"


#define FLAG_ACK_VALID	0x1
#define FLAG_HAS_DATA	0x2
#define FLAG_SYN	0x4
#define FLAG_FIN	0x8


#define SWP_SEND_TIMEOUT 0x1
#define SWP_SEND_TIMEOUT_ON_TIMEOUT 0x10
#define SWP_SEND_FASTRETRANSMIT 0x100
#define SWP_SEND_FASTRECOVERY 0x1000


#define args( list )                    list
#define DECLARE_DO_MSG_FUN( fun )           DO_MSG_FUN  fun
#define DECLARE_DO_EVT_FUN( fun )	    DO_EVT_FUN  fun
#define DECLARE_DO_SWP_FUN ( fun )	    DO_SWP_FUN  fun
#define DECLARE_DO_TIMER_FUN( fun ) 	    DO_TIMER_FUN fun

struct sendQ_slot;
struct __swp_state_type;

typedef int DO_MSG_FUN args((Msg *m));
typedef void DO_EVT_FUN args((struct sendQ_slot *slot, int e_type));
typedef void DO_TIMER_FUN args((uint64_t time));
typedef void DO_SWP_FUN args((struct __swp_state_type *state, void *args));



typedef uint32_t SwpSeqno;
#define MAX_SEQ ((uint32_t)-1)
typedef char hbuf;

typedef struct {
    SwpSeqno	SeqNum;
    SwpSeqno	AckNum;
    uint8_t	HdrLen:6;
    uint8_t 	padding1:6;
    uint8_t 	Flags:4;
    uint16_t	Length;
    uint16_t 	AdvertisedWindow;
} SwpHdr;

#define HLEN sizeof(SwpHdr)
#define HLENMAX 63


#ifndef __SWS
#define __SWS 33
#endif

#ifndef __RWS
#define __RWS 33
#endif
typedef struct event_type_timeout {
    uint64_t time;
    int event_type;
    DO_EVT_FUN *callback_fun;
    struct sendQ_slot *callback_arg;
} TimeoutEvent;
#define EVENT_ID_TIMEOUT 0x01

typedef struct event_type_congestion {
    uint64_t time; // of congestion event
    int n_packets;
    DO_SWP_FUN *callback_fun;
    void *callback_arg;
} CongestionEvent;
#define EVENT_ID_CONGESTION 0x02

typedef union eventstype {
    TimeoutEvent e_timeout;
    CongestionEvent e_congestion;
} EventsType;


typedef struct event_type {
    int id;
    EventsType event;
} Event;

/*
typedef struct event_type {
  uint64_t time;
  int event_type;
  DO_EVT_FUN *callback_fun;
  struct sendQ_slot *callback_arg;
} Event;
*/
typedef struct {
    uint32_t sampleMean;
    uint32_t sampleVar;
    uint8_t n_samples;

    uint64_t sampleRTT;

    uint64_t estimatedRTT;
    uint64_t estimatedRTTdev;
    uint64_t timeout;
} TimeoutState;


struct __swp_opts_type;
struct __swp_sack_data_type;
struct __swp_sack_opt;
struct __swp_state_type;
struct __swp_echo_state;
#include "swp_options.h"



struct __event_queue_type;
struct SocketInterface;

typedef enum { SWP_SLOW_START, SWP_CONGESTION_AVOIDANCE } SenderState;

typedef struct __swp_state_type {

    /* sender side state: */
    SwpSeqno	LAR; /* Last acknowledgement received */
    SwpSeqno	LFS; /* Last frame sent */

    sem_t sendWindowNotFull;
    volatile uint8_t CongWin;
    volatile int8_t WindowSize;
    volatile uint8_t Threshold;
    uint8_t MaxSendWin;
    volatile SenderState CongestionMode;

    //Semaphore sendWindowNotFull;
    SwpHdr	hdr;
    uint8_t :0;
    struct sendQ_slot {
        uint16_t i_sendq;
        uint8_t duplicateAck;
        uint8_t SACKed;
        Event * timeout;
        uint64_t volatile * timeoutInterval; /* Timeout shared among all send_slots*/
        Msg msg;
    } sendQ[__SWS];
    volatile uint64_t timeout;

    /* receiver side state: */
    SwpSeqno	NFE; /* Next frame expected */
    SwpSeqno	LAF; /* Largest Acceptable Frame */
    struct recvQ_slot {
        uint16_t i_recvq;
        int16_t	received;
        Msg	msg;
    } recvQ[__RWS];
    SwpECHOState ECHO_state;

    DO_MSG_FUN *deliver_fun;

    SwpHdrSACKopt SACK_opts;
    SwpSACKState SACK_state;

    struct SocketInterface *sock_addrs;
    //struct __event_queue_type *EQ;
    int16_t delayedAck;
    TimeoutState RTT_est;

    struct __event_queue_type * volatile EQH_ptr;
    struct __event_queue_type * volatile EQT_ptr;
    struct sendQ_slot *volatile *RTT_sampled;

} SwpState;

extern uint64_t start_time;

extern void congestion_timer(uint64_t);
extern void proc_timer(uint64_t);

#endif
