#ifndef __linklayer_h_
#define __linklayer_h_
#include "transport.h"

#include "msg.h"

struct SocketInterface
{
    int fd;
    struct sockaddr_in src;
    struct sockaddr_in dst;
};


struct SocketInterface * LinkLayer(struct sockaddr_in* address, int is_server);





#endif
