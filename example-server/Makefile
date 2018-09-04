CC = clang
CFLAGS = -g

all: parallel-server

clean:
	rm -f parallel-server

parallel-server: parallel-server.c
	$(CC) $(CFLAGS) -o parallel-server parallel-server.c -lpthread
