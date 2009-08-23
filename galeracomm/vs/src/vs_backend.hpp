//
//
// VSBackend
//
//

#include "galeracomm/address.hpp"
#include "galeracomm/protolay.hpp"

struct VSBackendDownMeta : ProtoDownMeta {
    bool is_sync;
    VSBackendDownMeta(const bool is) : is_sync(is) {}
};

class VSBackend : public Protolay {
public:
    enum Flags {F_DROP_OWN_DATA = 0x1};
protected:
    Address addr;
    enum State {CLOSED, CONNECTED} state;
    Flags flags;
protected:
    VSBackend() : addr(), state(CLOSED), flags(static_cast<Flags>(0)) {}
public:
    
    virtual ~VSBackend() {
    }
    Address get_self() const {
	return addr;
    }
    void set_flags(Flags f) {
	flags = static_cast<Flags>(f | flags);
    }
    Flags get_flags() const {
	return flags;
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
