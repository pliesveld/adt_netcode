#ifndef __swp_h_
#define __swp_h_
#include <sys/types.h>

#define FAIL 0
#define SUCCESS 1
#define SUCCESS_MSG_DROPPED 2
#define SUCCESS_DATA_DELIVERED 3
#define SUCCESS_ACK_RECV 4
#define SUCCESS_DELAYED_ACK 5
#define SUCCESS_MSG_BUFFERED 7
#define SUCCESS_GAP_FILLED 9
#define SUCCESS_XFER_COMPLETE 11

static struct __swp_state_type * volatile swp_state;

extern int sendLINK(Msg *m);

int load_swp_hdr(SwpHdr *hdr,char *buf);
void store_swp_hdr(SwpHdr *hdr,char *buf);

void append_swp_options(SwpHdr *hdr, char *opts, uint8_t opts_len);

SwpState *SWP_create(struct sockaddr_in*, int);

int sendWindow(SwpState *, Msg *);
void recvWindow(SwpState *, Msg *);

int recvDataMainLoop(SwpState *s);
void senderOptSack(SwpState *, struct __swp_sack_data_type *);

void registerHLP(SwpState *,int (callback)(Msg *));
void registerLINK(SwpState *, struct SocketInterface *);

void free_SWP(SwpState *);

void debugBuffer(SwpState *);
void debugWindow(SwpState *);


void congestion_event(uint64_t);
void congestion_timer(uint64_t);

void swpFastRecovery(struct sendQ_slot *slot);

void register_timers();

void congestion_timeout();
#endif

