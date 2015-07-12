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

using connection_coroutine = co::symmetric_coroutine<void>;
using poller_coroutine = co::symmetric_coroutine<connection_co_status>;

struct connection_data
{
	int fd;
	connection_co_status status;
};

static void connection_co_func(connection_coroutine::yield_type& yield, poller_coroutine& poller, connection_data& data)
{
	auto yielder = bind(yield, ref(poller), placeholders::_1);

	char buffer[256];
	io_status result;
	while(true)
	{
		size_t count;
		result = read_n_co(yielder, data.fd, buffer, sizeof(buffer), count);
		if(result == IO_FAIL)
			break;
		result = write_n_co(yielder, data.fd, buffer, sizeof(buffer), count);
		if(result == IO_FAIL)
			break;
	}

	yield(poller, STATUS_DONE);
}

struct connection
{
	connection_coroutine co;
	connection_data data;
}

static void poller_co_func(poller_coroutine::yield_type& yield, poller_coroutine& this_co, int epoll_fd, atomic_bool& should_run)
{
	map<int, connection> cons;
	while(should_run.load())
	{
		struct epoll_event events[8];
		int num_events = epoll_wait(epoll_fd, events, 8, 50);
		for(int i = 0; i < num_events; ++i)
		{
			int fd = events[i].data.fd;
		    uint32_t event_mask = events[i].events;
			auto it = cons.find(fd);
			if(it == cons.end())
			{
				it = cons.emplace(fd, connection{});
				it->second.co = bind(connection_co_func, placeholders::_1, ref(this_co), ref(it->second));
				it->second.data = {fd, STATUS_WAIT_READ};
			}

			connection_co_status status = it->second.data.status;
			if((status == STATUS_WAIT_READ && (event_mask & EPOLLIN)) || (status == STATUS_WAIT_WRITE && (event_mask & EPOLLOUT)))
			{
				yield(it->second.co);
				status = yield.get();
			}

			if(status == STATUS_DONE || status == STATUS_FAIL)
			{
				goto end_con;
			}

			continue;
			
		end_con:
			close(fd);
			cons.erase(it);
		}
	}
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
