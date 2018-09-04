CC = clang
CFLAGS = -g -lpthread

all: client

clean:
	rm -f client

client: client.c ui.c ui.h
	$(CC) $(CFLAGS) -o client client.c ui.c -lncurses -lm
