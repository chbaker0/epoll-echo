#include "con_thread.hpp"

#include <unordered_map>

#include <cstdint>
#include <cstdio>
#include <cstddef>

#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/coroutine/all.hpp>

#include "io_utils.hpp"

using namespace std;
namespace co = boost::coroutines;

enum connection_co_status
{
	STATUS_ERROR = -1,
	STATUS_DONE = 0,
	STATUS_WAIT_READ,
	STATUS_WAIT_WRITE
};

template <typename YieldType>
static io_status read_n_co(YieldType& yield, int fd, void *buf, std::size_t count, std::size_t& count_out)
{
	io_status result;
	while((result = read_n(fd, buf, count, count_out)) == IO_AGAIN && count_out == 0)
	{
		yield(STATUS_WAIT_READ);
	}
	return result;
}

template <typename YieldType>
static io_status write_n_co(YieldType& yield, int fd, const void *buf, std::size_t count, std::size_t& count_out)
{
	io_status result;
	while((result = write_n(fd, buf, count, count_out)) == IO_AGAIN && count_out == 0)
	{
		yield(STATUS_WAIT_WRITE);
	}
	return result;
}

void echo_connection_co_func(co::symmetric_coroutine<connection_co_status>::yield_type& yield)
{
	auto read_yielder =
	[
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
