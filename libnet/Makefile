CC = gcc
CFLAGS = -g -Wall

all: libtimer

library32: libnet.c libnet.h $(MAKEFILE)
	$(CC) $(CFLAGS) -m32 -c libnet.c
	rm -f libnet32.a
	ar -r libnet32.a libnet.o
	
library64: libnet.c libnet.h $(MAKEFILE)
	rm -f *.o
	$(CC) $(CFLAGS) -m64 -c libnet.c
	rm -f libnet64.a
	ar -r libnet64.a libnet.o

libtimer: libtimer.h libtimer.c
	rm -f *.o
	$(CC) $(CFLAGS) -m32 -c libtimer.c
	ar -r libtimer.a libtimer.o

clean:
	rm -rf *.o libnet32.a libnet64.a
