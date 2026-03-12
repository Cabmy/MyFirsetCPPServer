CXX = g++
CXXFLAGS = -std=c++11 -Wall -g -O2 -Isrc

TARGETS = server client

LIB_SRC = $(wildcard src/*.cpp)

all: $(TARGETS)

server: server.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

client: client.cpp src/util.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf $(TARGETS)

.PHONY: all clean