#include "con_thread.hpp"

#include <unordered_map>

#include <cstdint>
#include <cstdio>
#include <cstddef>

#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

enum io_result
{
	IO_SUCCESS = 0,
	IO_AGAIN,
	IO_FAIL
};

template <typename T>
static T * add_bytes(T *ptr, std::size_t bytes)
{
	return const_cast<T*>(static_cast<const T*>(static_cast<const char*>(ptr) + bytes));
}

static io_result write_n(int fd, const void *buf, size_t count, size_t& count_out)
{
	size_t count_total = count;
	while(count > 0)
	{
		ssize_t result = write(fd, buf, count);
		if(result >= 0)
		{
			count -= result;
			buf = add_bytes(buf, result);
		}
		else if(errno == EINTR)
		{
			continue;
		}
		else
		{
			count_out = count_total - count;
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				return IO_AGAIN;
			else
				return IO_FAIL;
		}
	}
	return IO_SUCCESS;
}

static io_result read_n(int fd, void *buf, size_t count, size_t& count_out)
{
	size_t count_total = count;
	while(count > 0)
	{
		ssize_t result = read(fd, buf, count);
		if(result >= 0)
		{
			count -= result;
			buf = add_bytes(buf, result);
		}
		else if(errno == EINTR)
		{
			continue;
		}
		else
		{
			count_out = count_total - count;
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				return IO_AGAIN;
			else
				return IO_FAIL;
		}
	}
	return IO_SUCCESS;
}

void con_thread_func(int epoll_fd, const std::atomic_bool& run)
{
	struct con_state
	{
		std::array<char, 512> data;
		std::size_t size;
		bool writable;

		con_state(): size(0), writable(false) {}
	};
	std::unordered_map<int, con_state> con_data;

	char buffer[256];
	
	int con_sock;
	while(run.load())
	{
		struct epoll_event events[8];
		int num = epoll_wait(epoll_fd, events, 8, 50);
		for(int i = 0; i < num; ++i)
		{
			fprintf(stderr, "Handling input\n");
			int fd = events[i].data.fd;
			uint32_t ev = events[i].events;
			if(ev & EPOLLRDHUP || ev & EPOLLHUP || ev & EPOLLERR)
			{
				fprintf(stderr, "Closed\n");
				close(fd);
				con_data.erase(fd);
				continue; // Go to next fd
			}

			con_state& s = con_data[fd];
			
			if(ev & EPOLLOUT)
			{
				fprintf(stderr, "EPOLLOUT\n");
				size_t written = 0;
				if(s.size > 0)
				{
					io_result result = write_n(fd, &s.data[0], s.size, written);
					if(result == IO_FAIL)
					{
						fprintf(stderr, "Closed\n");
						close(fd);
						con_data.erase(fd);
						continue;
					}
				
					if(written < s.size && written > 0)
					{
						std::copy(s.data.begin() + written, s.data.begin() + s.size, s.data.begin());
						s.size -= written;
					}
					else if(written == s.size)
					{
						s.size = 0;
					}
				
					if(result == IO_SUCCESS)
					{
						s.writable = true;
					}
					else
					{
						s.writable = false;
					}
				}
				else
				{
					s.writable = true;
				}
			}

			if(ev & EPOLLIN)
			{
				fprintf(stderr, "EPOLLIN\n");
				io_result read_result = IO_SUCCESS, write_result = IO_SUCCESS;
				while(s.writable)
				{
					size_t count = 0;
					read_result = read_n(fd, buffer, sizeof(buffer), count);
				    if(read_result == IO_FAIL)
						break;

					if(count > 0)
					{
						size_t written = 0;
						write_result = write_n(fd, buffer, count, written);
						if(write_result == IO_FAIL)
							break;

						if(write_result == IO_AGAIN)
						{
							std::copy(buffer + written, buffer + count, s.data.begin());
							s.size = count - written;
							s.writable = false;
						}
					}
					if(read_result == IO_AGAIN)
						break;
				}
				if(read_result == IO_FAIL || write_result == IO_FAIL)
				{
					fprintf(stderr, "Closed\n");
					close(fd);
					con_data.erase(fd);
					continue;
				}
				if(read_result == IO_SUCCESS)
				{
				    if(s.size < s.data.size())
					{
						size_t count = 0;
						read_result = read_n(fd, &s.data[0], s.data.size() - s.size, count);
						if(read_result == IO_FAIL)
						{
							close(fd);
							con_data.erase(fd);
							continue;
						}

						s.size += count;
					}
				    // Just discard any more bytes once fd buffer is full
					while(read_result == IO_SUCCESS)
					{
						size_t count = 0;
						read_result = read_n(fd, buffer, sizeof(buffer), count);
					}
					if(read_result == IO_FAIL)
					{
						fprintf(stderr, "Closed\n");
						close(fd);
						con_data.erase(fd);
						continue;
					}
				}
			}

		    // struct epoll_event new_event;
		    // new_event.events = EPOLL_EVENTS;
			// new_event.data.fd = fd;
			// epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &new_event);
		}
	}
}
