CC = clang
CFLAGS = -g

all: DIRSRV

clean:
	rm -f DIRSRV

DIRSRV: DIRSRV.c
	$(CC) $(CFLAGS) -o DIRSRV DIRSRV.c -lncurses -lm
