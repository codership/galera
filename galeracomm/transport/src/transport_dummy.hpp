
class DummyTransport : public Transport {

    std::deque<ReadBuf*> in;
    std::deque<ReadBuf*> out;

public:

    DummyTransport(Poll *p) : in(), out() {}

    ~DummyTransport() {
	for (std::deque<ReadBuf*>::iterator i = in.begin(); i != in.end(); ++i)
	    (*i)->release();
	for (std::deque<ReadBuf*>::iterator i = out.begin(); i != out.end(); ++i)
	    (*i)->release();
    }
    
    size_t get_max_msg_size() const {
	return 1U << 31;
    }
    void connect(const char *addr) {
    }
    void close() {
    }
    void listen(const char *addr) {
	throw FatalException("Not applicable");
    }
    Transport *accept(Poll *, Protolay *) {
	throw FatalException("Not applicable");
    }

    int handle_down(WriteBuf *wb, const ProtoDownMeta *dm) {
	out.push_back(wb->to_readbuf());
	return 0;
    }

    void pass_up(WriteBuf *wb, const ProtoUpMeta *um) {
	in.push_back(wb->to_readbuf());
    }

    ReadBuf* get_out() {
	if (out.empty())
	    return 0;
	ReadBuf* rb = out.front();
	out.pop_front();
	return rb;
    }


};
