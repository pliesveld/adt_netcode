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

#include <semaphore.h>
#include <linux/sem.h>
#include <errno.h>

#include <libnet.h>

#include "msg.h"
#include "linklayer.h"
#include "transport.h"
#include "xfer.h"
#include "event.h"

struct __swp_sack_data_type;
#include "swp.h"
#include "swp_options.h"
#include "comm.h"

static struct sendQ_slot * volatile RTT_slot = { NULL };
static int sendSWP(SwpState *state, Msg* frame);
static int deliverSWP(SwpState *state, Msg *frame);
static int swpInWindow(SwpSeqno seqno, SwpSeqno min, SwpSeqno max);
static uint8_t receiver_advertised_window(SwpState *);

static inline int WindowSize(SwpState *s);
static inline int WindowLength(SwpState *s);

int sendWindow(SwpState *s, Msg *m)
{
    int ret;
    dbprintf(stderr,1,DB_PRINT_ALWAYS,"pre-sendWindow:");
    debugWindow(s);
    ret = sendSWP(s,m);
    dbprintf(stderr,1,DB_PRINT_ALWAYS,"post-sendWindow:");
    debugWindow(s);
    return ret;
}

void recvWindow(SwpState *s, Msg *m)
{
    dbprintf(stderr,1,DB_PRINT_ALWAYS,"pre-recvWindow:");
    debugWindow(s);
    switch(deliverSWP(s,m))
    {
    default:
    case FAIL:
        exit(1);
        break;
    case SUCCESS:
    case SUCCESS_MSG_DROPPED:
    case SUCCESS_DATA_DELIVERED:
        break;
    };
    dbprintf(stderr,1,DB_PRINT_ALWAYS,"post-recvWindow:");
    debugWindow(s);
}

static int deliverHLP(SwpState *s, Msg *m)
{
    return (*s->deliver_fun)(m);

}

void prepare_ack(Msg *m, uint8_t , SwpSeqno nfe);

int recvDataMainLoop(SwpState *s)
{
    int cli_len, res, swp_res;
    Msg pkt;
    Msg ackMsg;
    struct sockaddr_in cli_addr;
    fd_set s_set;
#ifdef NAEGLE_ACK
    struct timeval delayedAck = { 0, 500000 };
    int ack_awaiting_transmission = 0;
#endif

    while (1)
    {
        uint8_t opt_len;
        char opt_buffer[sizeof(SwpHdrSACKopt)];
        cli_len = sizeof(cli_addr);
        s->delayedAck = 1;

        res = recvfrom(s->sock_addrs->fd, (void *)&pkt.buffer,SIZE, 0,
                       (struct sockaddr *) &cli_addr, (socklen_t *) &cli_len);
        if (res == -1)
        {
            perror("recvfrom");
            exit(-1);
        }
        memcpy(&s->sock_addrs->src,&cli_addr,cli_len);
        pkt.bytes_used = res;
        swp_res = deliverSWP(s, &pkt);

        while(1)
        {
            int nowait_res;
            nowait_res = recvfrom(s->sock_addrs->fd, (void *)&pkt.buffer,
                                  SIZE, MSG_DONTWAIT,
                                  (struct sockaddr *) &cli_addr, 
                                  (socklen_t *) &cli_len);

            if( nowait_res == -1 )
            {
                if( errno != EAGAIN )
                {
                    perror("recvfrom");
                    exit(4);
                }
                break;
            }

            memcpy(&s->sock_addrs->src,&cli_addr,cli_len);
            pkt.bytes_used = nowait_res;
            swp_res = deliverSWP(s, &pkt);
        }

        switch(swp_res) 
        {
        default:
            dbprintf(stderr,0,DB_PRINT_ALWAYS,"unknown error %d: ",swp_res);
            perror("recvfrom");
            exit(3);
            break;
        case SUCCESS_XFER_COMPLETE:
            prepare_ack(&ackMsg,0,s->NFE );
            sendLINK(&ackMsg);
            return 0;
        case SUCCESS_DELAYED_ACK:
            FD_ZERO(&s_set);
            FD_SET(s->sock_addrs->fd, &s_set);

            prepare_ack(&ackMsg, 0, s->NFE - 1);
            goto send_ack;

#ifdef NAEGLE_ACK
            ack_awaiting_transmission = 1;
            delayedAck.tv_sec = 0;
            delayedAck.tv_usec = 500000;
            if( (res = select( s->sock_addrs->fd + 1, &s_set, NULL, NULL, &delayedAck)) < 0)
            {
                perror("select");
                exit(1);
            }

            if( res != 0 && FD_ISSET(s->sock_addrs->fd,&s_set) )
                continue;
#endif

        case SUCCESS_MSG_DROPPED:
            //continue;
        case SUCCESS_GAP_FILLED:
            prepare_ack(&ackMsg, 0, s->NFE - 1);
            goto send_ack;
        case SUCCESS_MSG_BUFFERED:
            opt_len = swp_append_sack_opts( opt_buffer, &s->SACK_state, &s->SACK_opts);
            if( sizeof(SwpHdrSACKopt) < opt_len )
            {
                dbprintf(stderr,0,DB_PRINT_ALWAYS,"net msg buffer overflow\n");
                exit(-2);
            }
            prepare_ack(&ackMsg, opt_len, s->NFE - 1);
            memcpy(ackMsg.buffer + sizeof(SwpHdr), opt_buffer, opt_len);
            break;
        }

send_ack:
        sendLINK(&ackMsg);
    }

    return 0;
}



void senderOptSack(SwpState *s, struct __swp_sack_data_type *sack_block)
{
    SwpSeqno start, end;
    SwpSACKState *sack_state = &s->SACK_state;
    SwpSeqno highest_sacked_seqno = sack_state->LSS;

    for( start = sack_block->left_edge, end = sack_block->right_edge;
            start != end; ++start )
    {
        struct sendQ_slot *slot;
        if(start < s->LAR)
            continue;
        if(start > highest_sacked_seqno)
            highest_sacked_seqno = start;

        slot = &s->sendQ[start % __SWS];
        slot->SACKed = 1;
    }
    sack_state->LSS = highest_sacked_seqno;
}

/*
  if(winResp == SUCCESS_MSG_DROPPED	)
  {
     return;;
  }
  if(winResp == SUCCESS_GAP_FILLED || winResp == SUCCESS_MSG_BUFFERED || winResp == SUCCESS )
	 break;
  if((res = select( s->sock_addrs->fd + 1, &s_set, NULL, NULL,&delayedAck)) < 0)
  {
    perror("select");
    exit(1);
  }

  if(res == 0)
    break;
    }
  }
*/

SwpState *SWP_create(struct sockaddr_in *host, int is_server)
{
    int i,j = 0;
    Event *evt_array;
    SwpState *s = (SwpState *) malloc(sizeof(SwpState));
    evt_array = (Event *) calloc(__SWS, sizeof(Event));

    s->LAR = 0;
    s->LFS = 0;

    s->hdr.SeqNum = 0;
    s->hdr.AckNum = 0;
    s->hdr.Length = 0;
    s->hdr.Flags = 0;
    s->hdr.AdvertisedWindow = 0;
    s->deliver_fun = NULL;

    s->delayedAck = 0;

    for(i = 0, j = 0; i < __SWS; i++)
    {
        struct sendQ_slot *s_ptr = &s->sendQ[i];
        s_ptr->timeout = & evt_array[j++];
        s_ptr->duplicateAck = 0;
        s_ptr->SACKed = 0;

        initializeTimeoutEvent(s_ptr->timeout);
        s_ptr->i_sendq = i;
        s_ptr->timeoutInterval = &s->timeout;
        msgDestroy(&s_ptr->msg);
    }

    s->Threshold = 7;
    s->MaxSendWin = 54;
    s->CongWin = 1;

    s->MaxSendWin = UMAX(1, (__SWS-1)/2);
    s->CongestionMode = SWP_SLOW_START;

    sem_init(&s->sendWindowNotFull,0,s->MaxSendWin);
    for(s->WindowSize = s->MaxSendWin; s->WindowSize > 1; s->WindowSize--)
    {
        sem_wait(&s->sendWindowNotFull);
    }

    s->NFE = 1;
    s->LAF = 0;
    for(i = 0; i < __RWS; i++)
    {
        struct recvQ_slot *s_ptr = &s->recvQ[i];
        s_ptr->received = 0;
        msgDestroy(&s_ptr->msg);
        s_ptr->i_recvq = i;
    }

    s->sock_addrs = (struct SocketInterface *) LinkLayer(host,is_server);
    initializeEQ(&s->EQH_ptr, &s->EQT_ptr);

    initializeTimeoutState(&s->RTT_est);
    s->RTT_sampled = (struct sendQ_slot * volatile /* volatile */ * ) &RTT_slot;
    s->timeout = s->RTT_est.timeout;
    initialize_swp_sack_options(&s->SACK_opts);
    initialize_swp_sack_state(&s->SACK_state);
    initialize_swp_echo_state(&s->ECHO_state);
    debugWindow(s);
    swp_state = s;
    return s;
}

void store_swp_hdr(SwpHdr *hdr,char *buf);
int load_swp_hdr(SwpHdr *hdr, char *buf);

static int sendSWP(SwpState *state, Msg *frame)
{
    struct sendQ_slot *slot;
    char hbuf[HLEN + (256 - HLEN)];

    do
    {
wait_again:
        if( sem_wait(&state->sendWindowNotFull) < 0 )
        {
            if( errno == EINTR )
                goto wait_again;
            perror("sem_wait");
            exit(1);
        }
    } while(WindowSize(state) < 0);

    unregister_timer();

    state->hdr.SeqNum = ++state->LFS;
    SET_BIT(state->hdr.Flags,FLAG_HAS_DATA);
    REMOVE_BIT(state->hdr.Flags,FLAG_ACK_VALID);
    state->hdr.AckNum = 0;
    slot = &state->sendQ[state->hdr.SeqNum % __SWS];
    state->hdr.HdrLen = HLEN;
    store_swp_hdr(&state->hdr, hbuf);
    msgAddHdr(frame,hbuf,state->hdr.HdrLen);
    msgSaveCopy(&slot->msg,frame);
    ++state->WindowSize;
    EVT_TIME(EVT_TIMEOUT(slot->timeout)) = UMIN(3000000ULL,state->timeout);

    recordRTT( state->timeout );

    if(!*state->RTT_sampled)
    {
        *state->RTT_sampled = slot;
    }

    slot->timeout = evSchedule(&swpTimeout, (struct sendQ_slot *) slot, (int)SWP_SEND_TIMEOUT);

    sendLINK(frame);
    register_timer(swp_state->EQH_ptr->time,&proc_timer);
    return WindowSize(state);
}
static inline void updateRTT(SwpState *state, struct sendQ_slot *slot,uint64_t );

static int deliverSWP(SwpState *state, Msg *frame)
{
    SwpHdr hdr;
    int header_length = HLEN;
    int header_opt_len = 0;
    char hbuf[HLEN + 16*8];
    dbprintf(stderr,2,DB_PRINT_ALWAYS,"deliverSWP: ");
    debugMsg(frame);

    msgStripHdr(hbuf,frame, &header_length);
    if( (header_opt_len = load_swp_hdr(&hdr, hbuf)) > 0)
    {
        char opt_buf[64];
        msgStripHdr(opt_buf, frame, &header_opt_len);
        swp_options(state,opt_buf,header_opt_len);
    }

    if (hdr.Flags & FLAG_ACK_VALID)
    {
        //uint64_t w_rtt_timestamp = 0ULL;
        /* received an acknowledgement--do SENDER side */
        register_timer(0,NULL);

        if(swpInWindow(hdr.AckNum, state->LAR + 1, state->LFS))
        {
            do {
                struct sendQ_slot *slot;
                uint64_t msg_timestamp;
                slot = (struct sendQ_slot *) &state->sendQ[++state->LAR % __SWS];

                msg_timestamp =  evCancel(slot->timeout);
                //updateRTTSampleMean(state->RTT_est, msg_timestamp);
                slot->duplicateAck = 0;
                slot->SACKed = 0;

                msgDestroy(&slot->msg);

                ++state->WindowSize;
                if(WindowSize(state) > 0 )
                {
                    if(sem_post(&state->sendWindowNotFull) < 0) {
                        perror("sem_post");
                        exit(1);
                    }
                }

                if(msg_timestamp != 0ULL)
                {
                    updateRTT(state,slot,msg_timestamp);
                }
            } while(state->LAR != hdr.AckNum);

        } else if(hdr.AckNum == state->LAR) {
            if(state->sendQ[state->LAR % __SWS].duplicateAck == 1) {
                struct sendQ_slot *slot;
                slot = &state->sendQ[(state->LAR + 1) % __SWS];
                if( EVT_IS_TIMEOUT(slot->timeout) && EVT_TIMEOUT(slot->timeout).event_type == SWP_SEND_TIMEOUT)
                {
                    debugEQ(NULL);
                    dbprintf(stderr,2,DB_PRINT_ALWAYS," fast retransmit...\n");
                    //debugEQ(swp_state->EQT_ptr);
                    sendLINK(&slot->msg);
                    swpFastRecovery(slot);
                }
                state->sendQ[state->LAR % __SWS].duplicateAck = -1;
            } else if(state->sendQ[state->LAR % __SWS].duplicateAck == 0) {
                state->sendQ[state->LAR % __SWS].duplicateAck = 1;
            }
        }
        if(state->LAR == state->LFS)
        {
            unregister_timer();
        } else {
            EventQueue *p = evQueueSearch(state->EQH_ptr, state->sendQ[ (state->LAR + 1) % __SWS].timeout);
            if(p != state->EQH_ptr)
            {
                register_timer(EVTQ_TIME(p),&proc_timer);
            } else {
                congestion_timer(0);
                register_timers();
            }
        }
    }

    if( hdr.Flags & FLAG_HAS_DATA)
    {
        struct recvQ_slot *slot;
        /* received data packet --- do RECEIVER side */
        slot = &state->recvQ[hdr.SeqNum % __RWS];
        if(!swpInWindow(hdr.SeqNum, state->NFE, state->NFE + __RWS - 1))
        {
            dbprintf(stderr,2,DB_PRINT_ALWAYS,"dropped msg^^^^:\n");
            /* drop the message */
            debugBuffer(state);
            return SUCCESS_MSG_DROPPED;
        }

        msgSaveCopy(&slot->msg,frame);
        slot->received = 1;
        if(hdr.SeqNum == state->NFE) {
            int i = 0;
            while( slot->received ) {
                if(!deliverHLP(state,&slot->msg))
                    return SUCCESS_XFER_COMPLETE;
                msgDestroy(&slot->msg);
                slot->received = 0;
                slot = &state->recvQ[++state->NFE % __RWS];
                if(state->LAF == hdr.SeqNum )
                    state->LAF = 0;
                i++;
            }
            prepare_sack_culmuative_ack(&state->SACK_state, &state->SACK_opts, state->NFE);
            /* send ACK: */
            if(state->LAF == 0 && i == 1) {
                state->delayedAck = 1;
                return SUCCESS_DELAYED_ACK;
            }

            state->delayedAck = 0;
            return SUCCESS_GAP_FILLED;
        } else {
            state->delayedAck = 0;
            if( state->LAF < hdr.SeqNum )
                state->LAF = hdr.SeqNum;

            prepare_sack(&state->SACK_state, &state->SACK_opts, hdr.SeqNum);
            return SUCCESS_MSG_BUFFERED;
        }
    }
    return SUCCESS;
}


uint8_t receiver_advertised_window(SwpState *s)
{
    if( s == NULL )
        s = swp_state;
    return s->LAF > s->NFE ? __RWS - (s->LAF - s->NFE) :
           __RWS - (s->NFE - s->LAF);

}

static int swpInWindow(SwpSeqno seqno, SwpSeqno min, SwpSeqno max)
{
    SwpSeqno pos, maxpos;
    pos = seqno - min;
    maxpos = max - min + 1;
    return pos < maxpos;
}

static inline void updateRTT(SwpState *state, struct sendQ_slot *slot, uint64_t msg_timestamp)
{
    int was_rtt = 0;
    uint64_t current_time;
    uint64_t messageRTT;
    char log_buf[128];
    char buf[32];

    if(*state->RTT_sampled == slot)
    {
        if(EVT_IS_TIMEOUT(slot->timeout) && EVT_TIMEOUT(slot->timeout).event_type == SWP_SEND_TIMEOUT) {
            messageRTT = (current_time = get_time()) - msg_timestamp;
            sprintf(log_buf,"deliverSWP Ack-RTT: sampleRTT %lu timeout was(swp):%lu (slots) %lu ",messageRTT,state->RTT_est.timeout, state->timeout);
            if( current_time < msg_timestamp )
            {
                dbprintf(stderr,0,DB_PRINT_ALWAYS,"updating future timestamp\n");
                congestion_timeout();
                return;
            }
            state->RTT_est.sampleRTT = messageRTT;
            state->timeout = nextTimeoutValue(&state->RTT_est);
            //state->timeout = UMIN(3000000LL,state->timeout);

            sprintf(buf,"timeout now:%lu:  ",state->timeout);
            strcat(log_buf, buf);
            dbprintf(stderr,1,DB_PRINT_ALWAYS,log_buf);
            debugMsg(&slot->msg);
        }
        was_rtt = 1;
        *state->RTT_sampled = NULL;
    }


    if(state->CongestionMode == SWP_SLOW_START || was_rtt)
    {
        if( state->CongWin + 1 > state->MaxSendWin)
            return;

        if(++state->CongWin > state->Threshold)
            state->CongestionMode = SWP_CONGESTION_AVOIDANCE;

        recordCongWin(state);

        //if(WindowSize(state) <= 0)
        //	return;

        //state->WindowSize++;
        if(WindowSize(state) > 0 )
            sem_post(&state->sendWindowNotFull);
    }
}

void store_swp_hdr(SwpHdr *hdr,char *buf)
{
    memset(buf,'\0',256);
    /*
      int i = 0;
      memcpy(&buf[i],&hdr->SeqNum,sizeof(SwpSeqno)); i += sizeof(SwpSeqno);
      memcpy(&buf[i],&hdr->AckNum,sizeof(SwpSeqno)); i += sizeof(SwpSeqno);
      memcpy(&buf[i],&hdr->Length,sizeof(uint16_t)); i += sizeof(uint16_t);
      memcpy(&buf[i],&hdr->Flags,sizeof(uint8_t)); i += sizeof(uint8_t);
      if(IS_SET(hdr->Flags,FLAG_ACK_VALID))
        sprintf(hdr->debug,"A%dA",hdr->AckNum);
      else if(IS_SET(hdr->Flags,FLAG_HAS_DATA))
        sprintf(hdr->debug,"D%dD",hdr->SeqNum);
      else
        memset(hdr->debug,'*',sizeof(hdr->debug));
    */

    memcpy((void*)buf,(void *)hdr,hdr->HdrLen);
}

int load_swp_hdr(SwpHdr *hdr,char *buf)
{
    memset(hdr,'\0',sizeof(SwpHdr));
    /*
      int i = 0;
      memcpy(&hdr->SeqNum,&buf[i],sizeof(SwpSeqno)); i += sizeof(SwpSeqno);
      memcpy(&hdr->AckNum,&buf[i],sizeof(SwpSeqno)); i += sizeof(SwpSeqno);
      memcpy(&hdr->Length,&buf[i],sizeof(uint16_t)); i += sizeof(uint16_t);
      memcpy(&hdr->Flags,&buf[i],sizeof(uint8_t)); i += sizeof(uint8_t);
    */
    memcpy((void*)hdr,(void *)buf,sizeof(SwpHdr));
    return ((int)hdr->HdrLen - sizeof(SwpHdr));
}

void prepare_ack(Msg *m, uint8_t opt_len, SwpSeqno nfe)
{
    SwpHdr t_header;
    uint8_t ack_len;
    ack_len = sizeof(SwpHdr) + opt_len;
    SET_BIT(t_header.Flags, FLAG_ACK_VALID);
    REMOVE_BIT(t_header.Flags, FLAG_HAS_DATA);
    t_header.Length = 0;
    t_header.HdrLen = ack_len;
    t_header.AckNum = nfe;
    t_header.AdvertisedWindow = __RWS - (nfe - swp_state->LAF);
    //sprintf(t_header.debug,"A%dA",t_header.AckNum);

    memcpy(m->buffer,&t_header, sizeof(SwpHdr));
    m->bytes_used = ack_len;
}

void registerHLP(SwpState *s,int (*callback)(Msg *))
{
    s->deliver_fun = callback;
}

void registerLINK(SwpState *state, struct SocketInterface *sock)
{
    state->sock_addrs = sock;
}

void free_SWP(SwpState *state)
{
    if(!state) return;

    if(sem_destroy(&state->sendWindowNotFull) < 0)
        perror("sem_destroy");

    free( state->sendQ[0].timeout ) ;

    if(state->sock_addrs)
    {
        free(state->sock_addrs);
        state->sock_addrs = NULL;
    }

    free(state);
}

static inline int WindowSize(SwpState *s)
{

    return s->LAR < s->LFS ?
           (int)s->CongWin - ((int)s->LFS - (int)s->LAR) :
           (int)s->CongWin - ((int)s->LAR - (int)s->LFS);

}

static inline int WindowLength(SwpState *s)
{

    return s->LAR < s->LFS ?
           ((int)s->LFS - (int)s->LAR) :
           ((int)s->LAR - (int)s->LFS);
}

void debugBuffer(SwpState *s)
{
    char buffer[1024];
    char buf[24];
    int i;
    dbprintf(stderr,1,DB_PRINT_ALWAYS,"Buffer State: NFE %d - %d LAF %d %s\n",
             s->NFE, s->NFE + __RWS - 1, s->LAF,
             (s->delayedAck ? "(DACK) " : ""));


    strcpy(buffer,"");
    for(i = 0; i < __RWS; ++i)
    {
        sprintf(buf,"%-2d ", i);
        strcat(buffer,buf);
    }
    strcat(buffer,"\n");
    dbprintf(stderr,1,DB_PRINT_ALWAYS,buffer);

    strcpy(buffer,"");
    for(i = 0; i < __RWS; ++i)
    {
        sprintf(buf,"%-2d ",
                (int)(s->recvQ[i].received));
        strcat(buffer,buf);
    }
    strcat(buffer,"\n");
    dbprintf(stderr,1,DB_PRINT_ALWAYS,buffer);

}

void debugWindow(SwpState *s)
{
    int sem_cnt = 0;


    sem_getvalue(&s->sendWindowNotFull,&sem_cnt);
    dbprintf(stderr,1,DB_PRINT_ALWAYS,"Window State: CongWin %u AdvWin %u Thres %u Max %u Sem %d :: LAR %u LFS %u Effective Window %d Inflight %d\n",
             s->CongWin, receiver_advertised_window(s), s->Threshold, s->MaxSendWin, sem_cnt,s->LAR, s->LFS, WindowSize(s), WindowLength(s));
}

void congestion_timeout()
{
    swp_state->CongWin /= 2;
    if( swp_state->CongWin == 0)
        swp_state->CongWin = 1;
    swp_state->Threshold = swp_state->CongWin;
    swp_state->CongWin = 1;
    swp_state->CongestionMode = SWP_SLOW_START;
    recordCongWin( (struct __swp_state_type * volatile )swp_state);
}

void congestion_event(uint64_t time)
{
    congestion_timeout();
    congestion_timer(time);
    register_timers();
    dbprintf(stderr,2,DB_PRINT_ALWAYS,"congestion timeout evt\n");
}

void congestion_timer(uint64_t time)
{
    EventQueue *p = NULL;
    EventQueue *p_next = NULL;
    EventQueue *p_new = NULL;
    int e_type;
    uint64_t congestionTimeout_time = 0;
    uint64_t congestionTimeout_delay = 0;
    Event *highest_sacked;

    p = swp_state->EQT_ptr;
    dbprintf(stderr,1,DB_PRINT_ALWAYS,"congestion_timer()\n");
    debugWindow(swp_state);
    dbprintf(stderr,2,DB_PRINT_ALWAYS,"Event Queue:\n");
    debugEQ(NULL);
    dbprintf(stderr,2,DB_PRINT_ALWAYS,"Congestion Queue:\n");
    debugEQ(p);


    if(p->next == p)
    {
        dbprintf(stderr,0,DB_PRINT_ALWAYS,"%d congestion_timer called, and EQ_Tail is an empty list!\n",time);
        exit(1);
    }

    highest_sacked = swp_state->LAR > swp_state->SACK_state.LSS ? NULL :  swp_state->sendQ[swp_state->SACK_state.LSS % __RWS].SACKed == 0 ? NULL : swp_state->sendQ[swp_state->SACK_state.LSS % __RWS].timeout;
    dbprintf(stderr,2,DB_PRINT_ALWAYS,"evt 0x%X: highest sacked\n", highest_sacked);

    p_next = backwardIterator(p);
    p = p_next;

    for( ; p != swp_state->EQT_ptr; p = p_next)
    {
        Event *evt = NULL;
        p_next = backwardIterator(p);
        evt = EVTQ_EVT(p);

        evQueueDelete(swp_state->EQT_ptr,p);
        swp_state->EQT_ptr->evt = (void*) ((intptr_t)swp_state->EQT_ptr->evt - 1);
        if( swp_state->EQT_ptr->prev == swp_state->EQT_ptr) {
            swp_state->EQT_ptr->time = 0;
            swp_state->EQT_ptr->evt = (void*) ((int)0);
        } else {
            swp_state->EQT_ptr->time = p->prev->time;
        }

        if( (e_type = EVT_TIMEOUT(evt).event_type) == SWP_SEND_TIMEOUT || e_type == SWP_SEND_TIMEOUT_ON_TIMEOUT  )
        {
            if( EVT_TIMEOUT(evt).callback_arg == *swp_state->RTT_sampled )
                *swp_state->RTT_sampled = NULL;
        }
        (*EVT_TIMEOUT(evt).callback_fun)(EVT_TIMEOUT(evt).callback_arg,EVT_TIMEOUT(evt).event_type);
        free(p);


        if(e_type == SWP_SEND_TIMEOUT_ON_TIMEOUT )
        {
            p_new = evQueueSearch(swp_state->EQH_ptr,evt);
            if( p_new != swp_state->EQH_ptr && congestionTimeout_time < EVTQ_TIME(p_new))
            {
                congestionTimeout_time = EVTQ_TIME(p_new);
                congestionTimeout_delay = EVT_TIME(EVT_TIMEOUT(evt));
                dbprintf(stderr,2,DB_PRINT_ALWAYS,"Using congestion time of %llu with a delay of %llu\n", congestionTimeout_time, congestionTimeout_delay);
            }

            p = p_next;
            break;
        }
        if (highest_sacked != NULL && EVT_IS_TIMEOUT(evt) && highest_sacked == (Event *) evt) {
            dbprintf(stderr,2,DB_PRINT_ALWAYS,"Highest Sacked slot hit --\n");
            p = p_next;
            break;
        }

    }

    if( swp_state->EQT_ptr->prev == swp_state->EQT_ptr )
        EVTQ_TIME(swp_state->EQT_ptr) = 0;

    /*
      for( ; p != swp_state->EQT_ptr; p = p_next)
      {
         char log_buf[256];
         char buf[64];
         Event *evt = NULL;
         uint64_t previous_time;
         uint64_t offset;
         p_next = backwardIterator(p);


         evt = EVTQ_EVT(p);
         strcpy(log_buf,"Adjusting timeout of ");
         sprintf(buf,"evt 0x%x ", evt);
         strcat(log_buf,buf);

         previous_time = EVTQ_TIME(p) - EVT_TIME(EVT_TIMEOUT(evt));
         offset = congestionTimeout_time - congestionTimeout_delay;
         sprintf(buf,"orig to @t%llu offset const %llu ", previous_time, offset);
         strcat(log_buf,buf);

         offset -= previous_time;
         sprintf(buf,"offset of evt %llu ", offset);
         strcat(log_buf,buf);

         sprintf(buf,"old timeout delay %llu @t%llu ", EVT_TIME(EVT_TIMEOUT(EVTQ_EVT(p))), EVTQ_TIME(p));
         strcat(log_buf,buf);
         EVT_TIME(EVT_TIMEOUT(EVTQ_EVT(p))) += 2*congestionTimeout_delay + offset;
         EVTQ_TIME(p) =  congestionTimeout_time + EVT_TIMEOUT(EVTQ_EVT(p)).time;
         sprintf(buf,"new timeout delay %llu @t%llu\n", EVT_TIME(EVT_TIMEOUT(EVTQ_EVT(p))), EVTQ_TIME(p));
         strcat(log_buf,buf);
         dbprintf(stderr,2,DB_PRINT_ALWAYS,log_buf);

         if( EVT_TIMEOUT(evt).callback_arg == *swp_state->RTT_sampled )
    		*swp_state->RTT_sampled = NULL;
      }


      swp_state->EQT_ptr->time = swp_state->EQT_ptr->prev->time;

      for(p = swp_state->EQH_ptr->prev ; p != swp_state->EQH_ptr; p = p_next)
      {
         Event *evt = NULL;
         uint64_t previous_time;
         uint64_t offset;
          p_next = backwardIterator(p);


         evt = EVTQ_EVT(p);
         previous_time =  EVTQ_TIME(p);
         if(previous_time > congestionTimeout_time )
    	continue;
         offset =  congestionTimeout_time - previous_time;
         offset =  congestionTimeout_time - previous_time;

          EVT_TIME(EVT_TIMEOUT(EVTQ_EVT(p))) += offset;
          //  congestionTimeout_time += EVT_TIMEOUT(EVTQ_EVT(p)).time;
         EVTQ_TIME(p) =  previous_time + EVT_TIMEOUT(EVTQ_EVT(p)).time;
         if( EVT_TIMEOUT(evt).callback_arg == *swp_state->RTT_sampled )
    		*swp_state->RTT_sampled = NULL;
      }


     // swp_state->EQH_ptr->time = swp_state->EQH_ptr->prev->time;
    */
    dbprintf(stderr,2,DB_PRINT_ALWAYS,"POST: congestion_timer congestionTimeout time (%llu)\n",congestionTimeout_time);
    debugWindow(swp_state);
    dbprintf(stderr,2,DB_PRINT_ALWAYS,"POST:--Event Queue:\n");
    debugEQ(NULL);
    dbprintf(stderr,2,DB_PRINT_ALWAYS,"POST:--Congestion Queue:\n");
    debugEQ(swp_state->EQT_ptr);


    register_timer(swp_state->EQH_ptr->time,&proc_timer);


}

void register_timers()
{
    if( swp_state->EQT_ptr->time )
    {
        if( swp_state->EQH_ptr->time && swp_state->EQH_ptr->time < swp_state->EQT_ptr->time ) {
            register_timer(swp_state->EQH_ptr->time, &proc_timer);
        } else {
            register_timer(swp_state->EQT_ptr->time, &congestion_timer);
        }
    } else if( swp_state->EQH_ptr->time ) {
        register_timer(swp_state->EQH_ptr->time, &proc_timer);
    } else {
        unregister_timer();
    }

}

void swpFastRecovery(struct sendQ_slot *slot)
{
    swp_state->Threshold = UMAX((swp_state->CongWin/2),1);
    swp_state->CongWin = swp_state->Threshold;
    swp_state->CongestionMode = SWP_CONGESTION_AVOIDANCE;

    recordCongWin(swp_state);

    if( *swp_state->RTT_sampled == slot )
        *swp_state->RTT_sampled = NULL;
    debugWindow(swp_state);
}
