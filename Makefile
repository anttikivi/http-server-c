CC     := gcc
CFLAGS := -g -lcurl -lz

server.out: app/server.c
	$(CC) $(CFLAGS) -o $@ $^
