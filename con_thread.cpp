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

// Don't judge me
using namespace std;
namespace co = boost::coroutines;

enum connection_co_status
{
	STATUS_ERROR = -1,
	STATUS_DONE = 0,
	STATUS_WAIT_READ,
	STATUS_WAIT_WRITE
};

template <typename SinkType>
static io_result read_n_co(SinkType& sink, int fd, void *buf, std::size_t count, std::size_t& count_out)
{
	io_result result;
	connection_co_status status;
	count_out = 0;
	while((result = read_n(fd, buf, count, count_out)) == IO_AGAIN && count_out == 0)
	{
		status = STATUS_WAIT_READ;
		sink(status);
	}
	return result;
}

template <typename SinkType>
static io_result write_n_co(SinkType& sink, int fd, const void *buf, std::size_t count, std::size_t& count_out)
{
	io_result result;
	connection_co_status status;
	count_out = 0;
	while((result = write_n(fd, buf, count, count_out)) == IO_AGAIN && count_out == 0)
	{
		status = STATUS_WAIT_WRITE;
		sink(status);
	}
	return result;
}

using connection_coroutine = co::asymmetric_coroutine<connection_co_status>;

class co_data_channel
{
private:
	int fd;
	connection_coroutine::push_type& sink;

public:
	co_data_channel(int _fd, connection_coroutine::push_type& _sink): fd(_fd), sink(_sink) {}

	io_result read_n(void *buf, std::size_t count, std::size_t& count_out)
	{
		return read_n_co(sink, fd, buf, count, count_out);
	}
	io_result write_n(const void *buf, std::size_t count, std::size_t& count_out)
	{
		return write_n_co(sink, fd, buf, count, count_out);
	}
};

struct connection_data
{
	int fd;
	connection_co_status status;
};

template <typename DataChannel>
static void connection_func(DataChannel&& ch)
{
	char buffer[256];
	io_result result;
	while(true)
	{
		size_t count = 0;
		result = ch.read_n(buffer, sizeof(buffer), count);
		if(result == IO_FAIL)
			break;
		size_t count_left = count;
		while(count_left)
		{
			result = ch.write_n(buffer, count_left, count);
			count_left -= count;
			if(result == IO_FAIL)
				break;
		}
	}
}

static void connection_co_entry(connection_coroutine::push_type& sink, connection_data& data)
{
	connection_co_status status;

	connection_func(co_data_channel(data.fd, sink));

	status = STATUS_DONE;
	sink(status);
}

struct connection
{
	connection_coroutine::pull_type co;
	connection_data data;
};

// This function pumps the epoll loop
void con_thread_func(int epoll_fd, const atomic_bool& should_run)
{
	// A map of file descriptors to connection structs
	map<int, connection> cons;
	
	while(should_run.load())
	{
		// Get some events from epoll
		struct epoll_event events[8];
		int num_events = epoll_wait(epoll_fd, events, 8, 50);
		
		for(int i = 0; i < num_events; ++i)
		{
			int fd = events[i].data.fd;
			uint32_t event_mask = events[i].events;
			
			// Look up file descriptor in our map<> of connections 
			auto it = cons.find(fd);
			
			// If we can't find it, create an entry
			if(it == cons.end())
			{
				it = cons.emplace(fd, connection{}).first;
				it->second.data = {fd, STATUS_WAIT_READ};
				it->second.co = connection_coroutine::pull_type(bind(connection_co_entry, placeholders::_1, ref(it->second.data)));
				if(it->second.co)
				{
					it->second.data.status = it->second.co.get();
				}
				else
				{
					it->second.data.status = STATUS_ERROR;
				}
			}

			connection_co_status status = it->second.data.status;

			if((event_mask & EPOLLHUP) || (event_mask & EPOLLRDHUP))
			{
				goto end_con;
			}
				
			// Check if the coroutine is waiting for the event we just got
			if((status == STATUS_WAIT_READ && (event_mask & EPOLLIN)) || (status == STATUS_WAIT_WRITE && (event_mask & EPOLLOUT)))
			{
				// Give the coroutine what it wants
				it->second.co();
				status = it->second.co.get();
			}

			// Check if connection should be closed
			if(status == STATUS_DONE || status == STATUS_ERROR)
			{
				// Please forgive me, Dijkstra
				goto end_con;
			}

			// Update the connection status
			it->second.data.status = status;

			// If everything is OK, continue the loop
			continue;
			
		end_con:
			// Close the socket (which automatically deregisters it from epoll), and remove our entry in the map
			close(fd);
			cons.erase(it);
		}
	}

	for(auto& p : cons)
	{
		close(p.first);
	}
}
