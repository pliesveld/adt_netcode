#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <strings.h>
#include <string.h>
#include <errno.h>

#include <semaphore.h>
#include <linux/sem.h>



#include <libnet.h>
#include "msg.h"
#include "transport.h"

#include "linklayer.h"
struct SocketInterface* LinkLayer(struct sockaddr_in* address, int is_server)
{
    int fd = -1;
    struct SocketInterface *newSocket = malloc(sizeof(struct SocketInterface));


    fd = socket(PF_INET,SOCK_DGRAM,0);
    memset(&newSocket->dst,'\0',sizeof(struct sockaddr));
    memset(&newSocket->src,'\0',sizeof(struct sockaddr));


    if(fd == -1)
    {
        perror("socket()");
        exit(1);
    }

    if( is_server )  {
        memcpy(&newSocket->src,address,sizeof(struct sockaddr_in));

        if(bind(fd, (struct sockaddr*)address, sizeof(struct sockaddr)) != 0)
        {
            perror("bind()");
            exit(1);
        }
    } else {
        memcpy(&newSocket->dst,address,sizeof(struct sockaddr_in));
    }


    newSocket->fd = fd;
    return newSocket;
}




#define MAXDATASIZE 1024
/*
int main(int argc, char **argv)
{
  uint16_t port = 6112;
  int sockfd;
  char buf[MAXDATASIZE];
  int i;
  struct sockaddr_in server_addr;
  struct sockaddr_in dst_addr;
  struct SocketInterface *sys_link = NULL;

  if (argc != 3)
    {
      fprintf(stderr,"usage: %s <server ip address> <server port> \n", argv[0]);
      exit(1);
    }


  i= atoi(argv[2]);

  bzero(&server_addr, sizeof(server_addr));

  dst_addr.sin_family = AF_INET;
  dst_addr.sin_port = htons((short)i);
  dst_addr.sin_addr.s_addr = inet_addr(argv[1]);

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons((short)0);




  sys_link = LinkLayer((struct sockaddr_in *)&server_addr);


  return 0;

}
*/

