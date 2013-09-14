#ifndef __xfer_h_
#define __xfer_h_

#include <stdio.h>
#include <stddef.h>


#define FILE_BUFFER_LEN 10*500

typedef struct filestate_type {
    FILE *file;
    char buffer[FILE_BUFFER_LEN];
    uint16_t b_used;
    off_t f_offset;

    uint8_t oldCongWin;
    struct timeval start_time;
    struct timeval previous_time;
} FileState;


int ReadFromFile(FileState *, Msg *);
int WriteToFile(FileState *,Msg *);
off_t getfilesize(FILE *fp);


FileState *OpenFile(const char *filename, const char *file_mode);
void CloseFile(FileState *fs);


extern void recordRTT(uint64_t);
extern void recordBW(FileState *f);
extern void recordCongWin(SwpState *);
#endif
