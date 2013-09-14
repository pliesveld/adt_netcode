#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <semaphore.h>
#include <linux/sem.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <libnet.h>

#ifdef __cplusplus
}
#endif

#include "msg.h"
#include "linklayer.h"
#include "transport.h"
#include "xfer.h"
#include "swp.h"
#include "event.h"

#include "comm.h"

#define MAXDATASIZE 1000

static FileState * volatile input;
static SwpState * volatile s;


void processAcks();
void processFin();

static volatile SwpSeqno lastAck;
static sem_t sem_fin;


void start_simulator(int argc, char ** argv)
{
    int i;
    uint16_t verbosity = 0;
    unsigned char
    variable_bw = 1,
    variable_rtt = 1,
    variable_reliable = 1,
    random = 1;
    for(i = 0; i < argc; i++)
    {
        if(!strcmp(argv[i],"-reliable"))
            variable_reliable = 0;
        else if(!strcmp(argv[i],"-const-bw"))
            variable_bw = 0;
        else if(!strcmp(argv[i],"-const-rtt"))
            variable_rtt = 0;
        else if(!strcmp(argv[i],"-no-random"))
            random = 0;
        else if(argv[i][0] == '-' && argv[i][1] == 'v') {
            int j = 1;
            while( argv[i][j] == 'v' )
            {
                j++;
            };
            verbosity = j - 1;
        }
    }


    set_debug_verbosity( verbosity, 0 );

    dbprintf(stderr,0,DB_PRINT_ALWAYS, "Simulator launch options bw: %s rtt: %s packetloss: %s randomize: %s%s\n",
             variable_bw == 1 ? "variable" : "constant",
             variable_rtt == 1 ? "variable" : "constant",
             variable_reliable == 1 ? "yes" : "no",
             random == 1 ? "yes" : "no",
             verbosity == 0 ? "" : verbosity == 1 ? " verbose" : verbosity == 2 ? " more verbose " : " debug verbose");


    init_net(variable_bw, variable_rtt, variable_reliable, random);
}


int main(int argc, char *argv[])
{
    char buf[MAXDATASIZE];
    int i;
    uint64_t now, then;
    double kbits;
    struct sockaddr_in server_addr; /* server's address information */


    if(sem_init(&sem_fin,0, 0) < 0) {
        perror("sem_init");
        exit(1);
    }

    start_simulator(argc, argv);

    if (argc < 2)
    {
        dbprintf(stderr,0,DB_PRINT_ALWAYS,"usage: %s <server ip address> <server port> [-v] [-vv] [-vvv] [-reliable] [const-bw] [-const-rtt] [-no-random]\n", argv[0]);
        exit(1);
    }


    i= atoi(argv[2]);

    bzero(&server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;      /* host byte order */
    server_addr.sin_port = htons((short)i); /* short, network byte order */
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);

    s = SWP_create(&server_addr,0);

    input = OpenFile("input.dat","r");
    register_read(&processAcks,s->sock_addrs->fd);

    then = get_time();

    while (1)
    {
        int clen;
        Msg frame;

        clen = ReadFromFile( input,&frame);
        frame.bytes_used = clen;
        debugWindow(s);

        if(feof(input->file))
        {
            SwpHdr lastheader;
            if(clen > 0)
            {
                sendWindow(s,&frame);
            }
            else if(sem_post(&sem_fin) < 0) {
                perror("sem_post");
                exit(1);
            }

            memcpy(buf,frame.buffer,HLEN);
            load_swp_hdr(&lastheader,buf);
            //dbprintf(stderr,1,DB_PRINT_ALWAYS, "Wait for ack #%d\n", lastheader.SeqNum);
            lastAck = s->LFS;
            register_read(&processFin,s->sock_addrs->fd);
            break;

        }

        sendWindow(s,&frame);

    }





sem_interrupted:
    if(sem_wait(&sem_fin) < 0) {
        if( errno == EINTR ) goto sem_interrupted;
        perror("sem_wait");
        exit(1);
    }


    now = get_time();
    kbits = (double) (input->f_offset * 8) / 1000.0;
    dbprintf(stderr,0,DB_PRINT_ALWAYS,"Time elapsed: %llums\n", (now - then)/1000LL);
    dbprintf(stderr,0,DB_PRINT_ALWAYS,"Overall transfer rate: %.2f kbps\n", kbits/((now - then)/1000000LL));



    sem_destroy(&sem_fin);

    close_net();
    close(s->sock_addrs->fd);
    free_SWP(s);
    CloseFile(input);
    return 0;
}

void processAcks()
{
    Msg m;
    int b = 0;
    socklen_t cli_len = sizeof(struct sockaddr_in);

    dbprintf(stderr,2,DB_PRINT_ALWAYS,"procAcks: recvfrom:");
    if((b = recvfrom(s->sock_addrs->fd,m.buffer,sizeof(m.buffer), MSG_DONTWAIT, (struct sockaddr*)&s->sock_addrs->src, &cli_len)) < 0)
    {
        if(errno == EAGAIN)
            return;

        perror("process acks: recvfrom");
        exit(1);
    }
    debugMsg(&m);
    m.bytes_used = b;
    recvWindow( s, &m);
}

void processFin()
{
    Msg m;
    int b = 0;
    socklen_t cli_len = sizeof(struct sockaddr_in);

    dbprintf(stderr,2,DB_PRINT_ALWAYS,"procFin: recvfrom:");
    if((b = recvfrom(s->sock_addrs->fd,m.buffer,sizeof(m.buffer), MSG_DONTWAIT, (struct sockaddr*)&s->sock_addrs->src, &cli_len)) < 0)
    {
        if(errno == EAGAIN)
            return;

        perror("process acks: recvfrom");
        exit(1);
    }

    debugMsg(&m);

    m.bytes_used = b;
    recvWindow( s, &m);

    if( s->LAR == s->LFS && (s->LAR) == lastAck )
    {
        if(sem_post(&sem_fin) < 0) {
            perror("sem_post");
            exit(1);
        }
    }

}

int sendLINK(Msg *m)
{

    dbprintf(stderr,2,DB_PRINT_ALWAYS,"sendLINK: ");
    debugMsg(m);

    if(u_sendto(s->sock_addrs->fd,m->buffer, m->bytes_used, 0, (struct sockaddr*)&s->sock_addrs->dst/*src*/, sizeof(struct sockaddr)) < 0)
    {
        perror("sendto");
        exit(1);
    }
    return SUCCESS;
}



