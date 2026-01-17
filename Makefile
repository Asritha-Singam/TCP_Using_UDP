CC = gcc
CFLAGS = -Wall -O2
LIBS = -lcrypto

all: client server

client: client.c
	$(CC) $(CFLAGS) client.c -o client $(LIBS)

server: server.c
	$(CC) $(CFLAGS) server.c -o server $(LIBS)

clean:
	rm -f client server *.o server_log.txt client_log.txt
