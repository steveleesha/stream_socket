CC = gcc
CFLAGS = -Wall -Wextra

all: client

client: client.c cJSON.c
	$(CC) $(CFLAGS) -o client client.c cJSON.c

clean:
	rm -f client 