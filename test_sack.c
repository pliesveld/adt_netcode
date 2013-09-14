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




#include "msg.h"
#include "linklayer.h"
#include "transport.h"
#include "xfer.h"
#include "event.h"
#include "swp.h"
#include "swp_options.h"

#include "transport.h"
#include "swp.h"
#include "swp_options.h"

void print_opts_block( SwpHdrSACKOptData *sack_block )
{
    fprintf(stderr,"%u-%u ", sack_block->left_edge, sack_block->right_edge);
}

void print_opts( SwpHdrSACKopt * s_opt)
{
    int len;
    fprintf(stderr, "Sack opts: ");
    if( (len = s_opt->SwpSACKHdr.opt_len) == 2)
    {
        fprintf(stderr, "none.");
    } else {
        int i;
        len -= 2;
        for(i = 0; i < 5; i++)
        {
            print_opts_block( &s_opt->SwpSACKdata[i] );
            len -= 8;
            if(len <= 0)
                break;
        }
    }
    fprintf(stderr,"\n");

}

void print_state( SwpSACKState *s )
{
    fprintf(stderr,
            "State: LSS %u, sack_blocks %u, idx_start %u\n",
            s->LSS, s->n_blocks, s->idx_last_block);
}


int main(int argc, char ** argv)
{
    SwpHdrSACKopt sack_opts;
    SwpSACKState sack_state;

    initialize_swp_sack_state(&sack_state);
    initialize_swp_sack_options(&sack_opts);

    prepare_sack(&sack_state, &sack_opts, 0);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack_culmuative_ack(&sack_state, &sack_opts, 0);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 2);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 4);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 9);

    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 3);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 5);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 1);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack_culmuative_ack(&sack_state, &sack_opts, 9);
    print_state(&sack_state);
    print_opts(&sack_opts);
    fprintf(stderr,"\n\n");


// initialize_swp_sack_state(&sack_state);
// initialize_swp_sack_options(&sack_opts);

    prepare_sack(&sack_state, &sack_opts, 2);
    prepare_sack(&sack_state, &sack_opts, 5);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 3);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 4);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack_culmuative_ack(&sack_state, &sack_opts, 4);
    print_state(&sack_state);
    print_opts(&sack_opts);
    fprintf(stderr,"\n\n");

// initialize_swp_sack_state(&sack_state);
// initialize_swp_sack_options(&sack_opts);

    prepare_sack(&sack_state, &sack_opts, 2);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 5);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 4);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 3);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 1);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack(&sack_state, &sack_opts, 0);
    print_state(&sack_state);
    print_opts(&sack_opts);
    prepare_sack_culmuative_ack(&sack_state, &sack_opts, 5);
    print_state(&sack_state);
    print_opts(&sack_opts);
    fprintf(stderr,"\n");



    return 0;
}

void senderOptSack(SwpState *swp_state, struct __swp_sack_data_type *sack_data)
{
}

