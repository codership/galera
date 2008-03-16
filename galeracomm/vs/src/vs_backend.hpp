//
//
// VSBackend
//
//

#include "gcomm/address.hpp"
#include "gcomm/protolay.hpp"

struct VSBackendDownMeta : ProtoDownMeta {
    bool is_sync;
    VSBackendDownMeta(const bool is) : is_sync(is) {}
};

class VSBackend : public Protolay {
protected:
    Address addr;
    enum State {CLOSED, CONNECTED} state;
protected:
    VSBackend() : state(CLOSED) {}
public:

    virtual ~VSBackend() {
    }
    Address get_self() const {
	return addr;
    }
    virtual void connect(const char *addr) = 0;
    virtual void close() = 0;
    virtual void join(const ServiceId id) = 0;
    virtual void leave(const ServiceId id) = 0;
    static VSBackend *create(const char *, Poll *, Protolay *);
    static VSBackend *create(const char *c, Poll *p) {
	return create(c, p, 0);
    }
};
