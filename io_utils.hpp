#ifndef IO_UTILS_HPP_INCLUDED
#define IO_UTILS_HPP_INCLUDED

#include <cstddef>

enum io_result
{
	IO_SUCCESS = 0,
	IO_AGAIN,
	IO_FAIL
};

io_result read_n(int fd, void *buf, std::size_t count, std::size_t& count_out);
io_result write_n(int fd, const void *buf, std::size_t count, std::size_t& count_out);

#endif // IO_UTILS_HPP_INCLUDED
