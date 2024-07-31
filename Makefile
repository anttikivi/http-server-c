CC     := gcc
CFLAGS := -lcurl -lz

server.out: app/server.c
	$(CC) $(CFLAGS) -o $@ $^
