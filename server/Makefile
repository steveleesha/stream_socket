CC = gcc
CFLAGS = -Wall -Wextra

all: server

server: server.c cJSON.c
	$(CC) $(CFLAGS) -o server server.c cJSON.c

clean:
	rm -f server 