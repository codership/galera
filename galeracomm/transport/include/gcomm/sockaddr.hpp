#ifndef SOCKADDR_HPP
#define SOCKADDR_HPP

#include <sys/socket.h>

#include <algorithm>
#include <string>
#include <sstream>

struct Sockaddr {
    sockaddr sa;
    size_t sa_len;

    Sockaddr(const sockaddr* s, const size_t s_len) {
	sa_len = s_len;
	memcpy(&sa, s, s_len);
    }
    Sockaddr(const unsigned char b) {
	sa_len = sizeof(sockaddr);
	memset(&sa, b, sa_len);
    }
    Sockaddr(const Sockaddr& s) {
	sa = s.sa;
	sa_len = s.sa_len;
    }
    Sockaddr() {
	sa_len = 0;
    }

    bool operator<(const Sockaddr& cmp) const {
	return memcmp(&sa, &cmp.sa, std::min(sa_len, cmp.sa_len)) < 0;
    }
    bool operator==(const Sockaddr& cmp) const {
	return sa_len == cmp.sa_len && memcmp(&sa, &cmp.sa, sa_len) == 0;
    }
    
    std::string to_string() const {
	std::ostringstream os;
	os << "Sockaddr(";
	for (size_t i = 0; i < sa_len; ++i)
	    os << static_cast<unsigned int>(reinterpret_cast<const char*>(&sa)[i]);
	os << ")";
	return os.str();
    }

    static const struct Sockaddr ADDR_INVALID;
    static const struct Sockaddr ADDR_ANY;
};



#endif // SOCKADDR_HPP
