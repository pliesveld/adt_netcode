#ifndef __msg_h_
#define __msg_h_



#define SIZE 500


typedef struct
{
    char buffer[SIZE];
    uint16_t bytes_used;
} Msg;


typedef struct {
    char *buffer;
    uint16_t length;
    uint16_t size;
} MsgDynamic;


//typedef MsgVariable Msg;


void msgAddHdr(Msg *frame, char *buffer, int buffer_len);
void msgStripHdr(char *,Msg *, int *);
void msgSaveCopy(Msg *message_dst,Msg *message_src);
void msgDestroy(Msg *m);


void debugMsg(Msg *);


#endif
