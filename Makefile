# Makefile

CXX = g++
CXXFLAGS = -std=c++11

all: server

server: ChatServer.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -f server
