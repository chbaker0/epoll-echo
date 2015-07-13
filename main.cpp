#include <thread>

#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <netinet/ip.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "socket_funcs.hpp"
#include "con_thread.hpp"

#define PORT "20123"

volatile bool sigint_received = false;
void sigint_handler(int)
{
	sigint_received = true;
}

int main()
{
	using namespace std;

	// Set up our SIGINT handler that will be used to shut down the program
	struct sigaction sig;
	memset(&sig, 0, sizeof(sig));
	sig.sa_handler = sigint_handler;
	if(sigaction(SIGINT, &sig, NULL) < 0)
	{
		perror("Could not register signal handler");
		return 1;
	}

	// Prepare epoll
	int epoll_fd = epoll_create(64);
	if(epoll_fd < 0)
	{
		perror("Could not create epoll fd");
		return 1;
	}

	int test_sock = open_tcp_listen_socket(PORT);
	if(test_sock < 0)
	{
		return 1;
	}

	// Start our connection handling thread
	std::atomic_bool run_con_thread(true);
	std::thread con_thread(std::bind(con_thread_func, epoll_fd, std::cref(run_con_thread)));

	// Accept loop
	int con_sock;
	while((con_sock = accept(test_sock, NULL, NULL)) >= 0 && !sigint_received)
	{
		// To properly use edge-triggered epoll, we must make the socket nonblocking
		if(make_nonblocking(con_sock))
		{
			continue;
		}

		// Add an epoll entry for the socket
		struct epoll_event ev;
		ev.events = EPOLL_EVENTS;
		ev.data.fd = con_sock;
		if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, con_sock, &ev) < 0)
		{
			perror("Couldn't register connection in epoll");
		}
	}

	printf("Exiting...\n");

	close(test_sock);

	run_con_thread.store(false);
	con_thread.join();

	return 0;
}
