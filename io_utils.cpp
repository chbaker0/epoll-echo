#include "io_utils.hpp"

#include <errno.h>
#include <unistd.h>

using namespace std;

template <typename T>
static T * add_bytes(T *ptr, std::size_t bytes)
{
	return const_cast<T*>(static_cast<const T*>(static_cast<const char*>(ptr) + bytes));
}

io_result write_n(int fd, const void *buf, size_t count, size_t& count_out)
{
	size_t count_total = count;
	while(count > 0)
	{
		ssize_t result = write(fd, buf, count);
		if(result > 0)
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
	count_out = count_total - count;
	return IO_SUCCESS;
}

io_result read_n(int fd, void *buf, size_t count, size_t& count_out)
{
	size_t count_total = count;
	while(count > 0)
	{
		ssize_t result = read(fd, buf, count);
		if(result > 0)
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
	count_out = count_total - count;
	return IO_SUCCESS;
}
