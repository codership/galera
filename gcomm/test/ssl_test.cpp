/* Copyrignt (C) 2014 Codership Oy <info@codership.com> */

#include "gcomm/protonet.hpp"
#include "gcomm/util.hpp"
#include "gcomm/conf.hpp"

#include <map>
#include <stdexcept>

static gu::Config conf;

class Client : public gcomm::Toplay
{
public:
    Client(gcomm::Protonet& pnet, const std::string& uri)
        :
        gcomm::Toplay(conf),
        uri_   (uri),
        pnet_  (pnet),
        pstack_(),
        socket_(pnet_.socket(uri)),
        msg_   ()
    {
        pstack_.push_proto(this);
        pnet_.insert(&pstack_);
    }

    ~Client()
    {
        pnet_.erase(&pstack_);
        pstack_.pop_proto(this);
        socket_->close();
    }

    void connect(bool f = false)
    {
        socket_->connect(uri_);
    }

    std::string msg() const
    {
        return std::string(msg_.begin(), msg_.end());
    }

    void handle_up(const void* id, const gcomm::Datagram& dg,
                   const gcomm::ProtoUpMeta& um)
    {
        if (um.err_no() != 0)
        {
            log_error << "socket failed: " << um.err_no();
            socket_->close();
            throw std::exception();
        }
        else
        {
            assert(id == socket_->id());
            msg_.insert(msg_.begin(), gcomm::begin(dg),
                        gcomm::begin(dg) + gcomm::available(dg));
        }
    }
private:
    gu::URI           uri_;
    gcomm::Protonet&  pnet_;
    gcomm::Protostack pstack_;
    gcomm::SocketPtr  socket_;
    gu::Buffer        msg_;
};


class Server : public gcomm::Toplay
{
public:
    Server(gcomm::Protonet& pnet, const std::string& uri)
        :
        gcomm::Toplay(conf),
        uri_(uri),
        pnet_(pnet),
        pstack_(),
        listener_(),
        smap_(),
        msg_("hello ssl")
    {
        pstack_.push_proto(this);
        pnet_.insert(&pstack_);
        listener_ = pnet_.acceptor(uri_);
    }

    ~Server()
    {
        pnet_.erase(&pstack_);
        pstack_.pop_proto(this);
    }

    void listen()
    {
        listener_->listen(uri_);
    }

    void handle_up(const void* id, const gcomm::Datagram& dg,
                   const gcomm::ProtoUpMeta& um)
    {
        if (id == listener_->id())
        {
            gcomm::SocketPtr socket(listener_->accept());
            if (smap_.insert(
                    std::make_pair(socket->id(), socket)).second == false)
            {
                throw std::logic_error("duplicate socket entry");
            }
            return;
        }

        std::map<const void*, gcomm::SocketPtr>::iterator si(smap_.find(id));
        if (si == smap_.end())
        {
            throw std::logic_error("could not find socket from map");
        }

        gcomm::SocketPtr socket(si->second);
        if (socket->state() == gcomm::Socket::S_CONNECTED)
        {
            gcomm::Datagram msg;
            msg.payload().resize(msg_.size());
            std::copy(msg_.begin(), msg_.end(), msg.payload().begin());
            socket->send(0, msg);
        }
        else if (socket->state() == gcomm::Socket::S_CLOSED ||
                 socket->state() == gcomm::Socket::S_FAILED)
        {
            std::cerr << "socket " << id << " failed" << std::endl;
            socket->close();
            smap_.erase(id);
        }
        else
        {
            std::cerr << "socket state: " << socket->state() << std::endl;
        }
    }

private:
    Server(const Server&);
    void operator=(const Server&);
    gu::URI                           uri_;
    gcomm::Protonet&                  pnet_;
    gcomm::Protostack                 pstack_;
    std::shared_ptr<gcomm::Acceptor>  listener_;
    std::map<const void*, gcomm::SocketPtr> smap_;
    const std::string                 msg_;
};



int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cerr << "usage: " << argv[0] << " <-s|-c> <conf> <uri>"
                  << std::endl;
        return 1;
    }

    gu::Config conf;
    gcomm::Conf::register_params(conf);
    conf.parse(argv[2]);
    std::unique_ptr<gcomm::Protonet> pnet(gcomm::Protonet::create(conf));

    if (std::string("-s") == argv[1])
    {
        Server server(*pnet, argv[3]);
        server.listen();
        while (true)
        {
            pnet->event_loop(gu::datetime::Period(1 * gu::datetime::Sec));
        }
    }
    else if (std::string("-c") == argv[1])
    {
        Client client(*pnet, argv[3]);
        client.connect();
        while (true)
        {
            pnet->event_loop(gu::datetime::Period(1*gu::datetime::MSec));
            std::string msg(client.msg());
            if (msg != "")
            {
                std::cout << "read message from server: '" << msg << "'" << std::endl;
                break;
            }
        }
    }
    return 0;
}
