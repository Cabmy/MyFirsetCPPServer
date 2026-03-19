CXX = g++
CXXFLAGS = -std=c++11 -Wall -g -O2 -Isrc

TARGETS = server client test

LIB_SRC = $(wildcard src/*.cpp)

all: $(TARGETS)

server: server.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

client: client.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: test.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf $(TARGETS)

.PHONY: all clean