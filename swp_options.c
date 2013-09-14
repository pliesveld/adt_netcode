
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
#include "transport.h"
#include "swp.h"

#include "swp_options.h"
#include "comm.h"

void initialize_swp_sack_options(SwpHdrSACKopt *s)
{
    int i = 0;
    SwpHdrSACKopt *s_opt = s;

    for(; i < 5; i++)
    {
        SwpHdrSACKOptData *s_block = &s_opt->SwpSACKdata[i];
        s_block->left_edge = 0;
        s_block->right_edge = 0;
    }
    s_opt->SwpSACKHdr.opt_kind = 5;
    s_opt->SwpSACKHdr.opt_len = 2;
}

void initialize_swp_sack_state(SwpSACKState *s)
{
    s->LSS = 0;
    s->n_blocks = 0;
    s->idx_last_block = 0;
}


void initialize_swp_echo_options(SwpHdrECHOopt *s_opt)
{
    s_opt->SwpEchoHdr.opt_kind = 6;
    s_opt->SwpEchoHdr.opt_len = 6;
    memset(&s_opt->SwpECHOdata,sizeof(s_opt->SwpECHOdata),0);
}

void initialize_swp_echo_reply_options(SwpHdrECHOopt *s_opt)
{
    s_opt->SwpEchoHdr.opt_kind = 7;
    s_opt->SwpEchoHdr.opt_len = 6;
    memset(&s_opt->SwpECHOdata,sizeof(s_opt->SwpECHOdata),0);
}

void initialize_swp_echo_state(SwpECHOState *s)
{
    memset(&s->echodata,sizeof(s->echodata),0);
    s->echopresent = 0;
}


void swp_echo_options(SwpState *s, char *buf, int opt_len);


static inline int swp_sack_oosegment_subset_block(
    SwpSeqno no,
    SwpHdrSACKOptData *blk )
{

    return ( no >= blk->left_edge && no < blk->right_edge );

}


static uint8_t swp_sack_construct_block(SwpHdrSACKopt *s_opt, uint8_t idx, SwpSeqno no)
{
    SwpHdrSACKOptData sack_blk;
    sack_blk.left_edge = no;
    sack_blk.right_edge = no + 1;

    if(idx >= MAX_OPT_SACK_LEN )
        return ((uint8_t)-1);

    memcpy(&s_opt->SwpSACKdata[idx], &sack_blk, sizeof(struct __swp_sack_data_type));
    return idx;
}

static inline void swp_sack_shift_blocks(SwpHdrSACKopt *s_opt)
{
    memmove(&s_opt->SwpSACKdata[0],
            &s_opt->SwpSACKdata[1], sizeof(SwpHdrSACKOptData)*MAX_OPT_SACK_LEN - 1);
}

inline uint8_t swp_sack_options_len(SwpHdrSACKopt *s_opt)
{
    return s_opt->SwpSACKHdr.opt_len;
}

inline uint8_t swp_sack_state_len(SwpSACKState *state)
{
    return state->n_blocks;
}


void prepare_sack(SwpSACKState *state, SwpHdrSACKopt *s_opt, SwpSeqno trigger_seq )
{
    uint8_t i = 0;
    uint8_t idx_first = ((uint8_t)-1);
    //uint8_t idx_next = state->idx_last_block;

    for(; i < state->n_blocks; ++i )
    {
        if( swp_sack_oosegment_subset_block(trigger_seq, &s_opt->SwpSACKdata[i] ))
        {
            idx_first = i;
            break;
        }


        if(trigger_seq + 1 == s_opt->SwpSACKdata[i].left_edge)
        {
            --s_opt->SwpSACKdata[i].left_edge;
            if(i > 0 && s_opt->SwpSACKdata[i - 1].right_edge ==
                    s_opt->SwpSACKdata[i].left_edge)
            {
                s_opt->SwpSACKdata[i - 1].right_edge = s_opt->SwpSACKdata[i].right_edge;
                s_opt->SwpSACKdata[i].left_edge = s_opt->SwpSACKdata[i-1].left_edge;
                memmove(&s_opt->SwpSACKdata[i - 1], &s_opt->SwpSACKdata[i],
                        sizeof(SwpHdrSACKOptData)*(state->n_blocks - i));
                --state->n_blocks;
                idx_first = i-1;
            } else {
                idx_first = i;
            }
            break;
        }

        if(trigger_seq == s_opt->SwpSACKdata[i].right_edge)
        {
            ++s_opt->SwpSACKdata[i].right_edge;
            if( i + 1 <= state->n_blocks && s_opt->SwpSACKdata[i].right_edge ==
                    s_opt->SwpSACKdata[i+1].left_edge)
            {
                //	s_opt->SwpSACKdata[i].right_edge = s_opt->SwpSACKdata[i+1].right_edge;
                s_opt->SwpSACKdata[i+1].left_edge = s_opt->SwpSACKdata[i].left_edge;
                memmove(&s_opt->SwpSACKdata[i], &s_opt->SwpSACKdata[i+1],
                        sizeof(SwpHdrSACKOptData)*(state->n_blocks - i - 1));
                --state->n_blocks;
            }
            idx_first = i;
            break;
        }


    }

    if(idx_first== ((uint8_t)-1))
    {
        uint8_t idx;
        if( (idx = swp_sack_construct_block(s_opt,i,trigger_seq)) != ((uint8_t)-1))
        {
            state->idx_last_block = idx;
            ++state->n_blocks;
        } else {
            swp_sack_shift_blocks(s_opt);
            state->idx_last_block = 4;
        }
    } else {
        state->idx_last_block = idx_first;

    }
    s_opt->SwpSACKHdr.opt_len = sizeof(SwpHdrSACKOptData)*state->n_blocks + 2;

}

void prepare_sack_culmuative_ack(SwpSACKState *state, SwpHdrSACKopt *s_opt, SwpSeqno left_edge )
{
    uint8_t n_emptied_blocks = 0;
    uint8_t i = 0;
    uint8_t blks = state->n_blocks;
    uint8_t mask_sack_block_valid = 0;
    uint8_t mask_sack_block_acked = 0;
    const uint8_t mask_bits[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00 } ;
    //const uint8_t mask_bits[] = { 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00 } ;



    for(i = 0; i < MAX_OPT_SACK_LEN; i++)
    {
        SwpHdrSACKOptData *sack_data = &s_opt->SwpSACKdata[i];
        if( sack_data->left_edge > left_edge )
        {
            SET_BIT( mask_sack_block_valid, mask_bits[i] );
            continue;
        }

        if( sack_data->right_edge <= left_edge )
            SET_BIT( mask_sack_block_acked, mask_bits[i] );

    }


    if( mask_sack_block_valid == 0 )
    {
        initialize_swp_sack_state(state);
        initialize_swp_sack_options(s_opt);
        return;
    }

    n_emptied_blocks = 0;

    for(i = 0; i < MAX_OPT_SACK_LEN && i < blks; i++)
    {
        if( IS_SET( mask_sack_block_valid, mask_bits[i] ) )
        {
            if( IS_SET( mask_sack_block_valid, mask_bits[i+1]) )
            {
                state->n_blocks -= n_emptied_blocks;
                n_emptied_blocks = 0;
                continue;
            } else if(i + 1 == blks ) {
                break;
            } else {
                memmove( &s_opt->SwpSACKdata[ i - n_emptied_blocks ], &s_opt->SwpSACKdata[i], n_emptied_blocks*sizeof(SwpHdrSACKOptData));
            }
        }

        ++n_emptied_blocks;
    }

    state->n_blocks -= n_emptied_blocks;


//s_opt->SwpSACKHdr.opt_len = 2 + sizeof(SwpHdrSACKOptData)*(mask_sack_block_valid%2);
    s_opt->SwpSACKHdr.opt_len = 2 + sizeof(SwpHdrSACKOptData)*(state->n_blocks);
}
#if 0

for(; i < blks; ++i)
{
    SwpHdrSACKOptData *sack_data = &s_opt->SwpSACKdata[i];
    if(sack_data->left_edge > left_edge)
        break;
    if(sack_data->right_edge < left_edge)
    {
        ++n_emptied_blocks;
        continue;
    }
    sack_data->left_edge = left_edge;
    state->idx_last_block = i;
    break;
}
state->n_blocks -= n_emptied_blocks;
memmove(&s_opt->SwpSACKdata[0],&s_opt->SwpSACKdata[i],( (blks*sizeof(SwpHdrSACKOptData)) - state->n_blocks*sizeof(SwpHdrSACKOptData)));
s_opt->SwpSACKHdr.opt_len = 2 + sizeof(SwpHdrSACKOptData)*state->n_blocks;
#endif


int swp_sack_oosegment(SwpSACKState *state, SwpSeqno no, SwpHdrSACKopt *s_opt)
{
    int idx_last_block = -1;
    int i = 0;

    if( state->LSS < no )
        state->LSS = no;


    for( ; i < state->n_blocks; i++)
    {
        SwpHdrSACKOptData *sack_data = &s_opt->SwpSACKdata[i];
        if( swp_sack_oosegment_subset_block( no, sack_data) )
        {
            idx_last_block = i;
            break;
        }
    }

    return idx_last_block;

}

uint8_t swp_append_sack_opts(char *hdr, SwpSACKState *s, SwpHdrSACKopt *s_opt)
{
    int i, j;
    uint8_t idx_first_block;
    uint8_t n_blocks;
    uint8_t s_opt_len;

    if( (n_blocks = s->n_blocks) == 0 ||
            (s_opt_len = s_opt->SwpSACKHdr.opt_len) == 2)
        return 0;

    idx_first_block = s->idx_last_block;

    memcpy(hdr, &s_opt->SwpSACKHdr, sizeof(SwpHdrOpt));
    s_opt_len -= 2;
    hdr += sizeof(SwpHdrOpt);

    i = idx_first_block;
    memcpy(hdr, &s_opt->SwpSACKdata[i], sizeof(SwpHdrSACKOptData));
    s_opt_len -= sizeof(SwpHdrSACKOptData);
    hdr += sizeof(SwpHdrSACKOptData);

    for(j = 1, i = 0; j < n_blocks && s_opt_len > 0; i = (i + 1)%MAX_OPT_SACK_LEN, j++ )
    {
        if( i == idx_first_block )
            continue;
        memcpy(hdr, &s_opt->SwpSACKdata[i], sizeof(SwpHdrSACKOptData));
        s_opt_len -= sizeof(SwpHdrSACKOptData);
        hdr += sizeof(SwpHdrSACKOptData);
    }

    s_opt->SwpSACKHdr.opt_len = (sizeof(SwpHdrOpt) + sizeof(SwpHdrSACKOptData)*j);

    return s_opt->SwpSACKHdr.opt_len;
}

void swp_sack_options(SwpState *s, char *buf, int opt_len);

void swp_options(SwpState *s, char *buf, int opt_len)
{
    int i = 0;

    SwpHdrOpt *opt_type = NULL;

    while( opt_len > 0)
    {
        opt_type = ((SwpHdrOpt *) &buf[i]);

        dbprintf(stderr,2,DB_PRINT_ALWAYS,"swp_options kind = %u: ", opt_type->opt_kind);

        switch (opt_type->opt_kind)
        {
        case 5:
            swp_sack_options(s,buf + sizeof(SwpHdrOpt), -2 + opt_type->opt_len );
            break;
        case 6:
        case 7:
            swp_echo_options(s, buf + sizeof(SwpHdrOpt), -2 + opt_type->opt_len );
            break;
        default:
            dbprintf(stderr,2,DB_PRINT_ALWAYS,"unknown");
            break;
        }

        i += opt_type->opt_len;
        opt_len -= i;
        dbprintf(stderr,2,DB_PRINT_ALWAYS,"\n");
    }
}

void print_opts_sack_block( SwpHdrSACKOptData *sack_block )
{
    dbprintf(stderr,2,DB_PRINT_ALWAYS,"%u-%u ", sack_block->left_edge, sack_block->right_edge);
}

void print_opts_sack( SwpHdrSACKopt * s_opt)
{
    int len;
    dbprintf(stderr,2,DB_PRINT_ALWAYS, "Sack opts: ");
    if( (len = s_opt->SwpSACKHdr.opt_len) == 2)
    {
        dbprintf(stderr,2,DB_PRINT_ALWAYS, "none.");
    } else {
        int i;
        len -= 2;
        for(i = 0; i <= MAX_OPT_SACK_LEN; i++)
        {
            print_opts_sack_block( &s_opt->SwpSACKdata[i] );
            len -= 8;
            if(len <= 0)
                break;
        }
    }
    dbprintf(stderr,2,DB_PRINT_ALWAYS,"\n");

}

void print_sack_state( SwpSACKState *s )
{
    dbprintf(stderr,2,DB_PRINT_ALWAYS,
             "State: LSS %u, sack_blocks %u, idx_start %u\n",
             s->LSS, s->n_blocks, s->idx_last_block);
}


void swp_sack_options(SwpState *s, char *buf, int opt_len)
{
    while( opt_len > 0)
    {
        SwpHdrSACKOptData *s_data = (SwpHdrSACKOptData *) buf;
        print_opts_sack_block(s_data);
        senderOptSack(s,s_data);
        opt_len -= sizeof(SwpHdrSACKOptData);
        buf += sizeof(SwpHdrSACKOptData);
    }
}


/*
 TODO: implement rtt-timestamp echo
*/
void swp_echo_options(SwpState *s, char *buf, int opt_len)
{
    while( opt_len > 0)
    {
        SwpHdrSACKOptData *s_data = (SwpHdrSACKOptData *) buf;
        opt_len -= sizeof(SwpHdrEchoOptData);
        buf += sizeof(SwpHdrEchoOptData);
    }
}
