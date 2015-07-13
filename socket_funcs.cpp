#include <cstdio>
#include <cstring>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

int open_tcp_listen_socket(const char *port)
{
	int sock = -1;
	
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	if(getaddrinfo(NULL, port, &hints, &res))
	{
		perror("Could not resolve addrinfo");
		goto cleanup;
	}

	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if(sock < 0)
	{
		perror("Could not open test socket");
		goto cleanup;
	}

	if(bind(sock, res->ai_addr, res->ai_addrlen))
	{
		perror("Could not bind test socket");
        goto cleanup;
	}

	if(listen(sock, 64))
	{
		perror("Could not listen on test socket");
		goto cleanup;
	}

	return sock;

cleanup:
	close(sock);
	return -1;
}

int make_nonblocking(int fd)
{
	int fl_old = fcntl(fd, F_GETFL);
	if(fl_old == -1)
	{
		perror("Could not get file status flags in make_nonblocking");
		return 1;
	}
	if(fcntl(fd, F_SETFL, fl_old | O_NONBLOCK) == -1)
	{
		perror("Could not make file nonblocking");
		return 1;
	}
	return 0;
}
