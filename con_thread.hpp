#ifndef CON_THREAD_HPP_INCLUDED
#define CON_THREAD_HPP_INCLUDED

#include <atomic>

#include <sys/epoll.h>

#define EPOLL_EVENTS (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET)

void con_thread_func(int epoll_fd, const std::atomic_bool& run);

#endif // CON_THREAD_HPP_INCLUDED
