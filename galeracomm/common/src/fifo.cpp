#include <galeracomm/fifo.hpp>
#include <galeracomm/exception.hpp>

#include <limits>

int Fifo::push_back(const WriteBuf *wb) 
{
    if (is_full() == false) {
	dq.push_back(wb->to_readbuf());
	return 0;
    } else {
	return EAGAIN;
    }
}

int Fifo::push_front(const WriteBuf *wb) 
{
    if (is_full() == false) {
	dq.push_front(wb->to_readbuf());
	return 0;
    } else {
	return EAGAIN;
    }
}

int Fifo::push_after(Fifo::iterator i, const WriteBuf *wb)
{
    if (is_full() == false) {
	if (i != dq.end())
	    ++i;
	dq.insert(i, wb->to_readbuf());
	return 0;
    } else {
	return EAGAIN;
    }
}

ReadBuf *Fifo::pop_front() {
    ReadBuf *rb = 0;
    if (size() > 0) {
	rb = *begin();
	dq.pop_front();
    }
    return rb;
}

int Fifo::alloc_fd(Fifo *fifo)
{
    if (fifo_map.size() == max_fds)
	throw DException("");
    do {
	last_fd = last_fd == std::numeric_limits<int>::max() ? 
	    0 : last_fd + 1;
    } while (fifo_map.find(last_fd) != fifo_map.end());
    if (fifo_map.insert(std::pair<int, Fifo *>(last_fd, fifo)).second == false)
	throw DException("");
    return last_fd;
}

void Fifo::release_fd(const int fd)
{
    fifo_map.erase(fd);
}

Fifo *Fifo::find(const int fd)
{
    std::map<int, Fifo *>::iterator i = fifo_map.find(fd);
    if (i != fifo_map.end())
	return i->second;
    return 0;
}



// Instantiate 
int Fifo::last_fd = -1;
size_t Fifo::max_fds = 1024;
std::map<int, Fifo *> Fifo::fifo_map;
