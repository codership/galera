#ifndef EXCEPTION_HPP
#define EXCEPTION_HPP


#include <exception>
#include <iostream>


class Exception : public std::exception {
    const char *msg;
public:
    Exception() throw() {}
    Exception(const char *file, const char *function, const int line, 
	      const char *umsg) throw() {
	std::cerr << file << ':' << function << ':' << line << ':' << umsg << "\n";
	msg = "exception";
    }
    Exception(const char *_msg) throw() : msg(_msg) {
	
    }
    
    const char *what() const throw() {
	return msg;
    }
};

#define DException(_msg_) Exception(__FILE__, __FUNCTION__, __LINE__, _msg_)

#endif // EXCEPTION_HPP
