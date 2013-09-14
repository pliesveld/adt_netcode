
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




#include <stddef.h>

#include "libnet.h"

#include "msg.h"

#include "transport.h"
#include "xfer.h"

FILE* f_bw = { NULL } ;
FILE* f_rtt = { NULL } ;
FILE* f_congWin = { NULL } ;







uint32_t pkt_num = 0;
uint64_t start_time = 0;
uint8_t oldCongWin = 0;
//taken from a stackoverflow page
unsigned long long EPOCH = 2208988800ULL;
const unsigned long long NTP_SCALE_FRAC = 4294967295ULL;

unsigned long long tv_to_ntp(struct timeval *tv)
{
    unsigned long long tv_ntp, tv_usecs;

    tv_ntp = tv->tv_sec + EPOCH;
    tv_usecs = (NTP_SCALE_FRAC * tv->tv_usec) / 1000000UL;

    return (tv_ntp << 32) | tv_usecs;
}





/* receiver: x: tiem since start (in ms); y: avg bw over all received data */
FILE *avg_bw()
{
    if(!f_bw)
        f_bw = fopen("avg_bw.plot","w");

    return f_bw;
}

/* sender: x: pkt num y: RTT used (in ms) */
FILE *rtt_plot()
{
    if(!f_rtt)
        f_rtt = fopen("rtt.plot", "w");
    return f_rtt;
}

/* sender: x: time since start (in ms) y: congestion window size (in pkts) */
FILE *congWin_plot()
{
    if(!f_congWin)
    {
        f_congWin = fopen("congestion.plot","w");
        start_time = get_time();
        oldCongWin = 0;
    }
    return f_congWin;
}

void recordRTT(uint64_t RTT_timeout)
{
    char buffer[32];
    int len;
    bzero(buffer,sizeof(buffer));
    len = sprintf(buffer,"%u %llu\n", ++pkt_num, (RTT_timeout/1000ULL) );
    fwrite(buffer,1,len,rtt_plot());
}


uint64_t record_time(struct timeval *t)
{
    gettimeofday(t,NULL);
    return tv_to_ntp(t);
}

int ReadFromFile(FileState *f, Msg *pkt)
{
    uint16_t bytes_read = 0;
    while(!feof(f->file))
    {
        bytes_read = fread(pkt->buffer,1,sizeof(pkt->buffer) - HLEN,f->file);
        f->f_offset += bytes_read;

        if(ferror(f->file) == -1)
        {
            perror("fread");
            exit(1);
        }

        pkt->bytes_used = bytes_read;
        return bytes_read;

    }

    return -1;
}


int WriteToFile(FileState *f,Msg *data)
{
    uint16_t bytes_wrote = 0;
    bytes_wrote = fwrite(data->buffer,1,data->bytes_used,f->file);

    if(bytes_wrote < 1) {
        perror("fwrite");
        exit(1);
    }
    f->b_used = bytes_wrote;
    f->f_offset += bytes_wrote;
    //recordBW(f,data);

    return bytes_wrote;
}


void recordBW(FileState *f)
{
    char progbuffer[48];
    char buffer[128];
    int len;
    struct timeval current_time;
    struct timeval progress_time;

    long double total_bw;
    long double curr_bw;

    long double seconds;
    long double interval;

    bzero(progbuffer,sizeof(progbuffer));
    bzero(buffer,sizeof(buffer));

    record_time(&current_time);

    timersub(&current_time,&f->start_time, &progress_time);

    timerclear(&current_time);
    timeradd(&current_time,&progress_time,&current_time);


    timersub(&progress_time,&f->previous_time,&progress_time);

    seconds = ((long double)current_time.tv_sec * 1000.000) + ((long double)current_time.tv_usec/1000.000);

    interval = ((long double)progress_time.tv_sec * 1000.00) + ((long double) progress_time.tv_usec/1000.00000);

    total_bw = ((long double) f->f_offset*1.0)/seconds;

    curr_bw = ((long double) f->b_used*1.00000)/interval;


    strcpy(progbuffer,"");
    sprintf(progbuffer,"%Lf ", seconds);
    strcat(buffer,progbuffer);

    sprintf(progbuffer,"%Lf\n",
            curr_bw/total_bw);
    strcat(buffer,progbuffer);

    len = strlen(buffer);
    fwrite(buffer,1,len,avg_bw());

    if(ferror(avg_bw())) {
        perror("fwrite");
        exit(1);
    }


    timerclear(&f->previous_time);
    timeradd(&current_time,&f->previous_time, &f->previous_time);

}



FileState *OpenFile(const char *filename, const char *file_mode)
{
    FileState *f = (FileState *) malloc(sizeof(struct filestate_type));

    if((f->file = (FILE *) fopen(filename,file_mode)) == NULL) {
        perror("fopen");
        exit(1);
    }
    if(!strcmp(file_mode,"w"))
    {
        record_time(&f->start_time);
        timerclear(&f->previous_time);

    }

    f->b_used = 0;
    f->f_offset = (off_t) 0;
    memset(f->buffer,'\0',FILE_BUFFER_LEN);
    return f;
}

void CloseFile(FileState *f)
{
    if(f_congWin) fclose(congWin_plot());
    if(f_rtt) fclose(rtt_plot());
    if(f_bw) fclose(avg_bw());

    if(f->file)
    {
        fclose(f->file);
        f->file = NULL;
    }
    //free(f);
}


off_t getfilesize(FILE *fp)
{
    struct stat file_stat;
    if(!fp)
        return 0;

    fstat(fileno(fp),&file_stat);
    return file_stat.st_size;

}



void recordCongWin(SwpState *s)
{
    char buffer[32];
    int len;
    uint64_t time;
    FILE *congFile = congWin_plot();
    time = get_time();

    time = time - start_time;

    bzero(buffer,sizeof(buffer));
    if(oldCongWin != s->CongWin)
        oldCongWin = s->CongWin;
    else
        return;


    len = sprintf(buffer,"%llu %u\n", time/1000LL, s->CongWin);
    fwrite(buffer,1,len,congFile);
}



