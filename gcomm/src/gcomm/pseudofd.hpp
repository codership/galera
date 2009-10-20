#ifndef _GCOMM_PSEUDOFD_HPP_
#define _GCOMM_PSEUDOFD_HPP_

namespace gcomm
{
    
    class PseudoFd
    {

    public:
        PseudoFd() : fd(alloc_fd()) { }
        ~PseudoFd() { release_fd(fd); }
        int get() const { return fd; }
    private:
        PseudoFd(const PseudoFd&);
        void operator=(const PseudoFd&);
        static int alloc_fd();
        static void release_fd(int);
        int fd;
    };

} // namespace gcomm

#endif // _GCOMM_PSEUDOFD_HPP_
