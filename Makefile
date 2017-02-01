
INCDIR = include
CFLAGS = -Wall \
         -pthread \
         -g

straph: src/straph.c src/locks.c
	$(CC) $(CFLAGS) -I $(INCDIR) $^ -o $@

src/straph.c: include/straph.h

clean:
	rm -f straph
