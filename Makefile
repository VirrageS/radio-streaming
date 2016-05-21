TARGET: master player

CC = gcc
CFLAGS = -Wall -O2 -std=gnu99 -pedantic-errors -lpthread

master: master.o err.o misc.o
	$(CC) $(CFLAGS) $^ -o $@

player: player.o header.o misc.o parser.o err.o stream.o
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean TARGET
clean:
	rm -f master player *.o *~ *.bak
