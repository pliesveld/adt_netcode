#ifndef __swp_options_h_
#define __swp_options_h_

#define MAX_HOPTSLEN ( HLENMAX - HLEN )

typedef struct __swp_opts_type
{
    uint8_t opt_kind;
    uint8_t opt_len;
} SwpHdrOpt;

typedef struct __swp_sack_data_type
{
    SwpSeqno left_edge;
    SwpSeqno right_edge;
} SwpHdrSACKOptData;
#define MAX_OPT_SACK_LEN 5

typedef struct __swp_sack_opt
{
    SwpHdrOpt SwpSACKHdr;
    SwpHdrSACKOptData SwpSACKdata[MAX_OPT_SACK_LEN + 1];
} SwpHdrSACKopt;

void initialize_swp_sack_options(SwpHdrSACKopt *);


typedef struct __swp_sack_state
{
    SwpSeqno LSS;
    uint8_t n_blocks;
    uint8_t idx_last_block;
} SwpSACKState;

void initialize_swp_sack_state(SwpSACKState *);

typedef struct __swp_echo_data_type
{
    uint32_t timestamp;
} SwpHdrEchoOptData;

typedef struct __swp_echo_opt
{
    SwpHdrOpt SwpEchoHdr;
    SwpHdrEchoOptData SwpECHOdata;
} SwpHdrECHOopt;

void initialize_swp_echo_options(SwpHdrECHOopt *);
void initialize_swp_echo_reply_options(SwpHdrECHOopt *s_opt);

typedef struct __swp_echo_state
{
    uint32_t echodata;
    int8_t   echopresent:1;
} SwpECHOState;

void initialize_swp_echo_state(SwpECHOState *);

void prepare_sack(SwpSACKState *state, SwpHdrSACKopt *s_opt, SwpSeqno trigger_seq );
int swp_sack_oosegment(SwpSACKState *, SwpSeqno, SwpHdrSACKopt *);
void prepare_sack_culmuative_ack(SwpSACKState *state, SwpHdrSACKopt *s_opt, SwpSeqno left_edge );

uint8_t swp_append_sack_opts(char *hdr, SwpSACKState *s, SwpHdrSACKopt *s_opt);


void print_opts_sack_block( SwpHdrSACKOptData *sack_block );
void print_opts_sack( SwpHdrSACKopt * s_opt);
#endif
