#ifndef SOCKET_FUNCS_HPP_INCLUDED
#define SOCKET_FUNCS_HPP_INCLUDED

int open_tcp_listen_socket(const char *port);
int make_nonblocking(int sock);

#endif // SOCKET_FUNCS_HPP_INCLUDED
