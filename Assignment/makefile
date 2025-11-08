CC=gcc
CFLAGS=-O2 -Wall

all: udp_client udp_server

udp_client: udp_client.c
	$(CC) $(CFLAGS) -o udp_client udp_client.c

udp_server: udp_server.c
	$(CC) $(CFLAGS) -o udp_server udp_server.c

clean:
	rm -f udp_client udp_server recv.bin
