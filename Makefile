TARGET: master player

CC		= gcc
CFLAGS	= -Wall -O2 -std=gnu99 -pedantic-errors

master: master.o header.o misc.o parser.o err.o
	$(CC) $(CFLAGS) $^ -o $@

player: player.o header.o misc.o parser.o err.o
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean TARGET
clean:
	rm -f master player *.o *~ *.bak
