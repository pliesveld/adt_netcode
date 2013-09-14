CC!=gcc
#FLAGS=-I. -L. -g -Wformat
FLAGS=-I. -L. -g -Wformat -Wcast-qual
LIBS=-lm -lpthread -pthread -lnet64

all:client server ppp_test

ppp_test:test_ppp.c server client
	gcc $(FLAGS) -o ppp_test test_ppp.c

server:artcp_server.c event.o linklayer.o swp.o xfer.o msg.o swp_options.o comm.o
	$(CC) $(FLAGS) -o server artcp_server.c event.o swp.o linklayer.o xfer.o msg.o swp_options.o comm.o $(LIBS)

client:artcp_client.c event.o linklayer.o swp.o xfer.o msg.o swp_options.o comm.o
	$(CC) $(FLAGS) -o client artcp_client.c event.o swp.o linklayer.o xfer.o msg.o swp_options.o comm.o $(LIBS)


msg.o:transport.h msg.c msg.h
	$(CC) -c msg.c $(FLAGS)

xfer.o:xfer.h xfer.c
	$(CC) -c xfer.c $(FLAGS)

event.o:msg.o event.h event.c
	$(CC) -std=c99 -c event.c $(FLAGS)

linklayer.o:linklayer.c linklayer.h
	$(CC) -c linklayer.c $(FLAGS)

swp.o:swp.h event.o swp.c 
	$(CC) -c swp.c $(FLAGS)

swp_options.o:swp.o swp_options.h swp_options.c
	$(CC) -c swp_options.c $(FLAGS)

comm.o:comm.h comm.c
	$(CC) -c comm.c $(FLAGS)


clean:
	rm -rf client server ppp_test msg.o xfer.o event.o swp.o swp_options.o linklayer.o comm.o
