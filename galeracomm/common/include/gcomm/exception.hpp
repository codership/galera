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
	std::cerr << "Exception: ";
	std::cerr << file << ':' << function << ':'; 
	std::cerr << line << ':' << umsg << "\n";
	msg = umsg;
    }
    Exception(const char *_msg) throw() : msg(_msg) {
	
    }
    
    const char *what() const throw() {
	return msg;
    }
};

/*!
 * Macro to throw exceptions with debug information
 */
#define DException(_msg_) Exception(__FILE__, __FUNCTION__, __LINE__, _msg_)

/*!
 * Type of exception which is recoverable.
 */
class RuntimeException : public Exception {
public:
    RuntimeException(const char *msg) : Exception(msg) {}
};

/*!
 * Type of exception which is unrecoverable.
 */
class FatalException : public Exception {
public:
    FatalException(const char *msg) : Exception(msg) {}
}; 

#endif // EXCEPTION_HPP
