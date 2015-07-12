CXX ?= g++
CXXFLAGS ?= -std=c++11 -Og -g -Wall -Wextra -pedantic

CXX_COMPILE = $(CXX) $(CXXFLAGS) -c
CXX_LINK = $(CXX) $(CXXFLAGS)

.PHONY: all
all: server_test

server_test: main.o socket_funcs.o con_thread.o io_utils.o
	$(CXX_LINK) main.o socket_funcs.o con_thread.o io_utils.o -lboost_coroutine -lboost_system -pthread -o server_test

con_thread.o: con_thread.cpp con_thread.hpp io_utils.hpp
	$(CXX_COMPILE) con_thread.cpp -o con_thread.o

io_utils.o: io_utils.cpp io_utils.hpp
	$(CXX_COMPILE) io_utils.cpp -o io_utils.o

socket_funcs.o: socket_funcs.cpp socket_funcs.hpp
	$(CXX_COMPILE) socket_funcs.cpp -o socket_funcs.o

main.o: main.cpp con_thread.hpp socket_funcs.hpp
	$(CXX_COMPILE) main.cpp -o main.o

.PHONY: clean
clean:
	rm -rf *.o server_test
