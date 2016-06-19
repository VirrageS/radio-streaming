TARGET: master player

CXX = g++
CXXFLAGS = -Wall -O2 -std=c++0x -pedantic -lpthread

master: master.o misc.o session.o sessions.o
	$(CXX) $(CXXFLAGS) $^ -o $@

player: player.o misc.o stream.o command.o
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: clean TARGET
clean:
	rm -f master player *.o *~ *.bak
