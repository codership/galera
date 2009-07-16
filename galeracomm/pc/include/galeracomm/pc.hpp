#ifndef PC_HPP
#define PC_HPP

#include <galeracomm/address.hpp>
#include <galeracomm/exception.hpp>

class PCId : public Serializable {
    std::string id;
public:
    PCId(const char *i) : id(i) {
	if (id.size() > 32)
	    throw DException("PC id must be at most 8 bytes (excluding '\0')");
    }
    ~PCId() {

    }
    bool operator==(const PCId& cmp) const {
	return id == cmp.id;
    }
};

class PCMemb : public Serializable {
    Address addr;
    std::string name;
    PCSeq seq_last;
public:
    enum State {
	CLOSED,    // 
	CONNECTING, //
	CONNECTED, // Connected and joined to VS group
	CREATING,  // Creating new pc
	JOINING,   // Joining to existing pc
	JOINED,    // Successfully joined to existing pc
	RECOVERY,  // Reserved for future use
	LEAVING    // Leaving pc
    } state;
    PCMemb(const Address a, const char *n, const State istate) : addr(a), name(n), state(istate) {
    }
    ~PCMemb() {
    }
};

typedef PCMembMap std::map<const Address, PCMemb>;

// Incarnation number of the PC
typedef uint32_t PCSeq;

typedef std::map<const Address, const ReadBuf *> PCUserStateMap;

class PCState : public Serializable {
    PCId id;           // Identifier of the primary component
    PCSeq seq;         // Sequence number of pc_last
    PCMembMap pc_last; // Last seen primary component
    ReadBuf *user_state_buf; // User state received from VS
public:
    PCState(const PCId i, PCSeq s, const PCMembMap pc_l, const ReadBuf *us) : 
	id(i), seq(s), pc_last(pc_l), user_state_buf(us) {
	
    }
    virtual ~PCState {
	
    }
    virtual size_t read(const void *buf, const size_t buflen, 
			const size_t offset) {
	
    }
    virtual size_t write(void *buf, const size_t buflen, const size_t offset) {

    }
    virtual size_t size() const {

    }
};

typedef PCStateMap std::map<const Address, PCState>;


struct PCUpMeta : public ProtoUpMeta {
    const VSMessage      *vsmsg; // VS message header
    const bool is_primary;       // Whether component is primary
    const PCMembMap      *comp;  // Component members
    const PCUserStateMap *user_states; // Map of user provided states
    
    PCUpMeta(const VSMessage *vm) : 
	vsmsg(vm), is_primary(true),
	comp(0), user_states(0) {}
    PCUpMeta(const bool isp, const PCMembMap *cm, const PCUserStateMap *us) :
	vsmsg(0), is_primary(isp), comp(cm), user_states(us) {}
};

class PC : public Protolay {

    // Note that these must be modified only by sending message over VS
    PCId pcid;            // The identifier of the primary component
    PCSeq seq;            // Sequence number of the current PC
    PCMembMap pc;         // Current primary component, can be modified only
    PCStateMap pc_states; // States of all members
    
    // VS stuff
    VS *vs;
    VSView *reg_view;      // Last regular view
    VSView *trans_view;    // Last transitional view
    
    // State
    PCMemb::State state;
    // User state data
    const Serializable *user_state;


    void handle_msg(const ReadBuf *rb, const size_t roff, const VSUpMeta *vum);
    void handle_view(const VSView *view);
    void handle_up(const ReadBuf *rb, const size_t roff, const ProtoUpMeta *um);


    Poll *poll;
public:
    PC(const char *conf, Poll *p, Protolay *up_ctx, 
       const Serializable *us) : 
	vs(0), reg_view(0), trans_view(0), user_state(us), poll(p) {
	
	vs = VS::create(conf, poll);
	if (up_ctx == 0)
	    throw DException("PC does not make sense without up_ctx");
	set_up_context(up_ctx);
	set_down_context(vs);
    }
    ~PC() {
	delete vs;
	delete reg_view;
	delete trans_view;
    }
    
    void connect();
    void close();

    void create(const char *id);
    void join(const char *id);
    void leave();
    
};


#endif // PC_HPP
