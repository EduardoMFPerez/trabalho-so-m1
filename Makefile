CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread

all: server.exe client.exe

server.exe: server.c++ common.h
	$(CXX) $(CXXFLAGS) server.c++ -o server.exe

client.exe: client.c++ common.h
	$(CXX) $(CXXFLAGS) client.c++ -o client.exe

clean:
	-del /q server.exe client.exe 2>nul
	-rm -f server.exe client.exe

.PHONY: all clean