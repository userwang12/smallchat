# Makefile

CXX = g++
CXXFLAGS = -std=c++17

all: server

server: ChatServer.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ -g

clean:
	rm -f server
