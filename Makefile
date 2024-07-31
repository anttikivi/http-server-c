CC     := clang
CFLAGS := -fsanitize=address -g -Wall -Wextra -Wpedantic -lpthread -std=c99 -O0

server.out: app/server.c
	$(CC) $(CFLAGS) -o $@ $^
