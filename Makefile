TARGET: master player

CC		= gcc
CFLAGS	= -Wall -O2 -std=gnu99 -pedantic-errors

master: master.o err.o header.o
	$(CC) $(CFLAGS) $^ -o $@

player: player.o err.o header.o
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean TARGET
clean:
	rm -f master player *.o *~ *.bak
