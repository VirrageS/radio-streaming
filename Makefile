TARGET: master player

CC = gcc
CFLAGS = -Wall -O2 -std=gnu99 -pedantic -pedantic-errors -lpthread

CXX = g++
CXXFLAGS = -Wall -O2 -std=c++0x -pedantic -lpthread

master: master.o misc.o session.o
	$(CXX) $(CXXFLAGS) $^ -o $@

player: player.o misc.o parser.o stream.o
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean TARGET
clean:
	rm -f master player *.o *~ *.bak
