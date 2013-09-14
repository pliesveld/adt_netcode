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



#include "msg.h"
#include "transport.h"
#include "comm.h"


void msgAddHdr(Msg *frame, char *header, int buffer_len)
{
    memmove(frame->buffer + buffer_len, frame->buffer, sizeof(frame->buffer) - buffer_len);
    memcpy(frame->buffer,header, buffer_len);

    frame->bytes_used += buffer_len;
}

void msgStripHdr(char *header, Msg *frame, int *header_len)
{
    memcpy(header,frame->buffer,*header_len);
    memmove(frame->buffer, frame->buffer + *header_len, sizeof(frame->buffer) - *header_len);
    frame->bytes_used -= *header_len;
}

void msgSaveCopy(Msg *message_dst,Msg *message_src)
{
    uint16_t src_len = message_src->bytes_used;
    if(message_dst == message_src)
        return;

    src_len = URANGE(sizeof(SwpHdr),message_src->bytes_used,sizeof(message_dst->buffer));
    msgDestroy(message_dst);
    memcpy(message_dst->buffer,message_src->buffer,src_len);
    message_dst->bytes_used = src_len;
}

void msgDestroy(Msg *m)
{
    memset(m,'\0',sizeof(Msg));
}


void debugMsg(Msg *m)
{

    SwpHdr dbg_hdr;
    char buffer[256];

    char buf_data[32];
    char buf_ack[32];

    memset(buf_data,'\0',sizeof(buf_data));
    memset(buf_ack,'\0',sizeof(buf_ack));
    memset(buffer,'\0',sizeof(buffer));


    memcpy(&dbg_hdr,m->buffer,sizeof(SwpHdr));

    sprintf(buf_data,"seq: %d",dbg_hdr.SeqNum);

    sprintf(buf_ack,"ack: %d",dbg_hdr.AckNum);




    sprintf(buffer,"%s %s len %d",
            IS_SET(dbg_hdr.Flags,FLAG_ACK_VALID) ? buf_ack : "",
            IS_SET(dbg_hdr.Flags,FLAG_HAS_DATA) ? buf_data : "",
            dbg_hdr.Length ? dbg_hdr.Length : dbg_hdr.HdrLen);


    dbprintf(stderr,1,DB_PRINT_ALWAYS,"%s\n",buffer);

}
