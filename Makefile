CXX ?= g++
CXXFLAGS ?= -std=c++11 -g -Wall -Wextra -pedantic

CXX_COMPILE = $(CXX) $(CXXFLAGS) -c
CXX_LINK = $(CXX) $(CXXFLAGS)

.PHONY: all
all: server_test

server_test: main.o socket_funcs.o con_thread.o
	$(CXX_LINK) main.o socket_funcs.o con_thread.o -pthread -o server_test

con_thread.o: con_thread.cpp con_thread.hpp
	$(CXX_COMPILE) con_thread.cpp -o con_thread.o

socket_funcs.o: socket_funcs.cpp socket_funcs.hpp
	$(CXX_COMPILE) socket_funcs.cpp -o socket_funcs.o

main.o: main.cpp con_thread.hpp socket_funcs.hpp
	$(CXX_COMPILE) main.cpp -o main.o

.PHONY: clean
clean:
	rm -rf *.o server_test
