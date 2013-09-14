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


#define MAXDATASIZE 1024

#include "msg.h"
#include "linklayer.h"
#include "transport.h"
#include "xfer.h"
#include "swp.h"

#include "comm.h"

int deliverHLP(Msg *);


static FileState *output;
static off_t total_bytes;
static SwpState *s;

off_t getStoppingCondition(const char *f_name)
{
    int fd;
    struct stat file_stat;
    if( (fd = open(f_name,O_RDONLY)) < 0)
    {
        perror("open");
        exit(1);
    }

    fstat(fd,&file_stat);
    close(fd);
    return file_stat.st_size;
}



int main(int argc, char *argv[])
{
    int port_number;
    uint16_t verbosity = 0;
    int i;
    struct timeval then;
    struct timeval now;

    struct sockaddr_in serv_addr ;
    int socket_fd;



    if (argc < 2)
    {
        fprintf(stderr,"Usage: udp_server <port number> [-v | -vv | -vvv ]\n");
        exit(1);
    }

    port_number = atoi(argv[1]);
    if (port_number < 1024)
    {
        fprintf(stderr,"Invalid port number. Port < 1024. Aborting ...\n");
        exit(1);
    }

    total_bytes = getStoppingCondition("input.dat");
    output = OpenFile("output.dat","w");

    bzero((void *)&serv_addr, sizeof(struct sockaddr_in));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port_number);


    for(i = 2; i < argc; i++)
    {
        if(argv[i][0] == '-' && argv[i][1] == 'v') {
            int j = 1;
            while( argv[i][j] == 'v' )
            {
                j++;
            }
            verbosity = j - 1;
        }
    }

    set_debug_verbosity( verbosity, 0 );

    fprintf(stderr,"Verbosity level: %s\n",
            verbosity == 0 ? "none." : verbosity == 1 ? "minimal." : verbosity == 2 ? "medium." : "debug.");


    s = SWP_create(&serv_addr,1);
    socket_fd = s->sock_addrs->fd;

    registerHLP(s,&deliverHLP);
    gettimeofday(&then,NULL);


    while(1)
    {
        if(!recvDataMainLoop(s))
            break;
    }

    dbprintf(stderr,0,DB_PRINT_ALWAYS,"Wrote a total of %lu bytes to disk.\n", output->f_offset);
    fclose(output->file);
    gettimeofday(&now, NULL);

    {
        double seconds = 0, kbits;
        seconds += now.tv_sec - then.tv_sec;
        kbits = (double) (total_bytes * 8) / 1000.0;
        dbprintf(stderr,0,DB_PRINT_ALWAYS,"Total time:   %.2f ms\n", seconds * 1000);
        dbprintf(stderr,0,DB_PRINT_ALWAYS,"Total kb:  %.2f kb\n", kbits);
        dbprintf(stderr,0,DB_PRINT_ALWAYS,"Overall transfer rate: %.2f kbps\n", kbits/ seconds );
    }

    close(socket_fd);
    exit(0);
}


int deliverHLP(Msg *m)
{
    int cnt = WriteToFile(output,m);
    recordBW(output);

    dbprintf(stderr,1,DB_PRINT_ALWAYS,"Wrote to disk %d bytes\n", cnt);
    if(output->f_offset >= total_bytes) return 0;
    return 1;
}

int sendLINK(Msg *m)
{

    dbprintf(stderr,2,DB_PRINT_ALWAYS,"sendLINK:");
    debugMsg(m);
    if(sendto(s->sock_addrs->fd,m->buffer, m->bytes_used, 0, (struct sockaddr*)&s->sock_addrs->src, sizeof(struct sockaddr)) < 0)
    {
        perror("sendto");
        exit(1);
    }
    return SUCCESS;
}


