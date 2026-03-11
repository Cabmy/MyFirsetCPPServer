CXX := g++
CXXFLAGS := -Wall -Wextra -O2

TARGETS := server client

all: $(TARGETS)

server: server.cpp util.cpp Epoll.cpp InetAddress.cpp Socket.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

client: client.cpp util.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf $(TARGETS)

.PHONY: all clean