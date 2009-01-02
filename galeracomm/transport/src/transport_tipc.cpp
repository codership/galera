
#include "transport_tipc.hpp"
#include "transport_common.hpp"

#include <linux/tipc.h>
#include <arpa/inet.h>

/**
 * Parse TIPC address to sockaddr struct.
 * Currently recognized address format is
 *
 *    tipc:<type>.<instance>
 *
 * Where 
 * - type is TIPC port name type field
 * - instance is TIPC port name instance field
 */
static bool tipc_addr_to_sa(const char* addr, struct sockaddr* s, 
			    size_t* s_size)
{
    sockaddr_tipc sa;
    long long tmp;
    uint32_t type;
    uint32_t instance;
    const char* ptr = addr;
    if (ptr == 0 || s == 0 || s_size == 0) {
	LOG_ERROR("Invalid input");
	return false;
    }
    
    if (strncmp(ptr, "tipc:", strlen("tipc:"))) {
	LOG_ERROR(std::string("Invalid URI scheme in ") + addr);
	return false;
    }
    
    ptr += strlen("tipc:");
    
    tmp = strtoll(ptr, 0, 0);
    if (tmp < 0 || tmp > 0xffffffffL) {
	LOG_ERROR(std::string("Invalid TIPC address: ") + addr);
	return false;
    }
    type = tmp;

    if (type < 64) {
	LOG_ERROR(std::string("Invalid TIPC name type (must be ge 64): ") 
		  + addr);
	return false;
    }    
    
    if ((ptr = strchr(ptr, '.')) == 0) {
	LOG_ERROR(std::string("Invalid TIPC addrss: ") + addr);
	return false;	
    }
    
    tmp = strtoll(addr, 0, 0);
    if (tmp < 0 && tmp > 0xffffffffL) {
	LOG_ERROR(std::string("Invalid TIPC addrss: ") + addr);
	return false;
    }    
    instance = tmp;
    
    sa.family = AF_TIPC;
    sa.addrtype = TIPC_ADDR_MCAST;
    sa.scope = TIPC_CLUSTER_SCOPE;
    
    sa.addr.name.domain = 0;
    sa.addr.name.name.type = type;
    sa.addr.name.name.instance = instance;

    memcpy(s, &sa, sizeof(sa));
    *s_size = sizeof(sa);

    return true;
}


void TIPCTransport::connect(const char* addr)
{
    sockaddr_tipc *tipc_sa = reinterpret_cast<sockaddr_tipc*>(&sa);
    if (tipc_addr_to_sa(addr, &sa, &sa_size) == false) {
	throw FatalException("TIPCTransport::connect()");
    }

    if (tsfd == -1) {
	if ((tsfd = socket(AF_TIPC, SOCK_SEQPACKET, 0)) == -1) {
	    int err = errno;
	    LOG_ERROR(std::string("socket() returned: ") + strerror(err));
	    throw FatalException("TIPCTransport::connect()");
	}

	sockaddr_tipc ts_sa;
	memset(&ts_sa, 0, sizeof(ts_sa));
	ts_sa.family = AF_TIPC;
	ts_sa.addrtype = TIPC_ADDR_NAME;
	ts_sa.addr.name.name.type = TIPC_TOP_SRV;
	ts_sa.addr.name.name.instance = TIPC_TOP_SRV;

	if (::connect(tsfd, reinterpret_cast<sockaddr*>(&ts_sa), 
		      sizeof(ts_sa)) == -1) {
	    int err = errno;
	    LOG_ERROR(std::string("connect() returned: ") + strerror(err));
	    throw FatalException("TIPCTransport::connect()");	
	}
    } else {
	throw FatalException("Multiple connect not supported yet");
    }

    tipc_subscr subscr = {{tipc_sa->addr.name.name.type, 
			   tipc_sa->addr.name.name.instance, 
			   tipc_sa->addr.name.name.instance},
			  TIPC_WAIT_FOREVER, TIPC_SUB_PORTS, {}};
    
    if (::send(tsfd, &subscr, sizeof(subscr), 0) != sizeof(subscr)) {
	int err = errno;
	LOG_ERROR(std::string("send() returned: ") + strerror(err));
	throw FatalException("TIPCTransport::connect()");		
    }

    if ((fd = socket(AF_TIPC, SOCK_RDM, 0)) == -1) {
	int err = errno;
	LOG_ERROR(std::string("socket() returned: ") + strerror(err));
	throw FatalException("TIPCTransport::connect()");
    }

    // Note:
    // Kernel module source and TIPC examples suggest that bind must be done
    // using addrtype TIPC_ADDR_NAME or TIPC_ADDR_NAMESEQ. However, 
    // TIPC_ADDR_MCAST seems to work too, don't know why.
    // Must verify this at some point.
    tipc_sa->addrtype = TIPC_ADDR_NAME;
    int ret;
    if ((ret = bind(fd, &sa, sa_size)) == -1) {
	int err = errno;
	LOG_ERROR(std::string("bind() returned: ") + strerror(err));
	throw FatalException("TIPCTransport::connect()");
    }
    tipc_sa->addrtype = TIPC_ADDR_MCAST;
    LOG_INFO(std::string("Bind: ") + to_string(ret));

    socklen_t local_sa_size = sizeof(local_sa);
    if (getsockname(fd, &local_sa, &local_sa_size) == -1) {
	int err = errno;
	LOG_ERROR(std::string("getsockname() returned: ") + strerror(err));
	throw FatalException("TIPCTransport::connect()");
    }
    assert(local_sa_size == sa_size);

    int droppable = 0;

    if (setsockopt(fd, SOL_TIPC, TIPC_SRC_DROPPABLE, &droppable, sizeof(droppable)) == -1) {
	int err = errno;
	LOG_ERROR(std::string("setsockopt() returned: ") + strerror(err));
	throw FatalException("TIPCTransport::connect()");
    }

    if (setsockopt(fd, SOL_TIPC, TIPC_DEST_DROPPABLE, &droppable, sizeof(droppable)) == -1) {
	int err = errno;
	LOG_ERROR(std::string("setsockopt() returned: ") + strerror(err));
	throw FatalException("TIPCTransport::connect()");
    }
    
    if (poll) {
	poll->insert(tsfd, this);
	poll->set(tsfd, PollEvent::POLL_IN);
	poll->insert(fd, this);
	poll->set(fd, PollEvent::POLL_IN);
	poll->set(fd, PollEvent::POLL_ERR);
	
    }
    state = TRANSPORT_S_CONNECTED;
    sockaddr_tipc *id_sa = reinterpret_cast<sockaddr_tipc*>(&local_sa);
    LOG_INFO(std::string("Connected to ") + addr + " port id " + 
	     to_string(id_sa->addr.id.ref) + ":" 
	     + to_string(id_sa->addr.id.node));
}

void TIPCTransport::close()
{
    if (tsfd == -1)
	return;
    
    if (poll)
	poll->erase(tsfd);
    closefd(fd);
    tsfd = -1;

    if (fd != -1) {
	if (poll)
	    poll->erase(fd);
        closefd(fd);
	fd = -1;
    }


    LOG_INFO("closed");
}

void TIPCTransport::close(const char* addr)
{
    if (fd == -1) {
	LOG_WARN("TIPCTransport::close(const char*) called but transport "
		 "not open");
	return;
    }
    size_t s_size;
    sockaddr s;
    sockaddr_tipc *tipc_s = reinterpret_cast<sockaddr_tipc*>(&s);
    if (tipc_addr_to_sa(addr, &s, &s_size) == false) {
	throw FatalException("TIPCTransport::connect()");
    }
    tipc_s->scope = -tipc_s->scope;
    if (bind(fd, &s, s_size) == -1) {
	int err = errno;
	LOG_ERROR(std::string("bind() returned: ") + strerror(err));
	throw FatalException("TIPCTransport::close()");
    }
}

void TIPCTransport::handle(int fd, PollEnum pe)
{
    if (pe & PollEvent::POLL_OUT) {
	LOG_DEBUG("Poll out");
    } else if (pe & PollEvent::POLL_ERR) {
	LOG_WARN("Poll err");
    } else if (pe & PollEvent::POLL_IN) {
	struct sockaddr_tipc source_sa;
	struct iovec iov = {recv_buf, max_msg_size};
	size_t iovlen = 1;
	msghdr msg = {&source_sa, sizeof(source_sa), &iov, iovlen, 0, 0, 0};
	ssize_t ret = recvmsg(fd, &msg, 0);
	LOG_TRACE(std::string("TIPCTransport::handle():recvms() returned ") +
		  to_string(ret));
	TransportNotification tn;
	tn.type = "tipc";
	tn.sa_size = sa_size;
	tn.local_sa = local_sa;
	tn.state = state;
	if (ret == -1) {
	    LOG_WARN(std::string("Transport failure: ") + strerror(errno));
	    state = TRANSPORT_S_FAILED;
	    tn.state = state;
	    tn.ntype = TRANSPORT_N_FAILURE;
	    pass_up(0, 0, &tn);
	} else if (fd == this->fd) {
	    memcpy(&tn.source_sa, &source_sa, sizeof(source_sa));
	    tn.ntype = TRANSPORT_N_SOURCE;
	    ReadBuf *rb = new ReadBuf(recv_buf, ret);
	    pass_up(rb, 0, &tn);
	    rb->release();
	} else if (fd == this->tsfd) {
	    if (ret != sizeof(tipc_event)) {
		LOG_WARN(std::string("Invalid sized topology server message, size ") + to_string(ret));
		return;
	    }
	    tipc_event *te = reinterpret_cast<tipc_event*>(recv_buf);
	    if (te->event == TIPC_PUBLISHED) {
		tn.ntype = TRANSPORT_N_SUBSCRIBED;
	    } else if (te->event == TIPC_WITHDRAWN) {
		tn.ntype = TRANSPORT_N_WITHDRAWN;
	    } else {
		LOG_WARN(std::string("Unknown event: ") + to_string(te->event));
		return;
	    }
	    
	    sockaddr_tipc source_sa;
	    source_sa.family = AF_TIPC;
	    source_sa.addrtype = TIPC_ADDR_ID;
	    source_sa.scope = TIPC_CLUSTER_SCOPE;
	    source_sa.addr.id = te->port;
	    source_sa.addr.name.domain = 0;
		
	    memcpy(&tn.source_sa, &source_sa, sizeof(source_sa));
	    pass_up(0, 0, &tn);
		
	} else {
	    LOG_FATAL(std::string("Invalid fd: ") + to_string(fd));
	    throw FatalException("TIPCTransport::handle()");
	}
	
    } else {
	LOG_WARN("Unknown poll event");
    }
}


int TIPCTransport::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    iovec iov[2];
    size_t iovlen = 0;
    msghdr msg;
    
    if (wb->get_hdrlen() > 0) {
	iov[iovlen].iov_base = const_cast<void*>(wb->get_hdr());
	iov[iovlen].iov_len = wb->get_hdrlen();
	iovlen++;
    }
    if (wb->get_len() > 0) {
	iov[iovlen].iov_base = const_cast<void*>(wb->get_buf());
	iov[iovlen].iov_len = wb->get_len();
	iovlen++;
    }
    
    msg.msg_name = &sa;
    msg.msg_namelen = sa_size;
    msg.msg_iov = iov;
    msg.msg_iovlen = iovlen;
    msg.msg_control = 0;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;
    
    int err = 0;
    int ret;
    if ((ret = sendmsg(fd, &msg, MSG_DONTWAIT)) == -1) {
	err = errno;
	LOG_WARN(std::string("sendmsg(): ") + strerror(err));
    } else if (ret == 0) {
	LOG_WARN(std::string("sendmsg(): "));
    }
    return err;
}


size_t TIPCTransport::get_max_msg_size() const
{
    return TIPC_MAX_USER_MSG_SIZE;
}

TIPCTransport::TIPCTransport(Poll *p) : fd(-1), tsfd(-1), sa_size(0), poll(p) {
    max_msg_size = TIPC_MAX_USER_MSG_SIZE;
    recv_buf = new unsigned char[max_msg_size];
}

TIPCTransport::~TIPCTransport()
{
    close();
    delete[] recv_buf;
}
