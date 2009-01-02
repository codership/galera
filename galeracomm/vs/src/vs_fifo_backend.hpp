#ifndef VS_FIFO_BACKEND_H
#define VS_FIFO_BACKEND_H

class VSFifoBackend : public VSBackend {
public:
    VSFifoBackend(Poll *p, Protolay *pr);
    ~VSFifoBackend();
    void handle_up(const int cid, const ReadBuf *rb, const size_t roff, 
		   const ProtoUpMeta *um);
    int handle_down(WriteBuf *wb, const ProtoDownMeta *dm);
    void connect(const char *be_addr);
    void close();
    void join(const ServiceId sid);
    void leave(const ServiceId sid);
};

#endif // VS_FIFO_BACKEND
