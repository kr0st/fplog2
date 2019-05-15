#include <sprot.h>
#include <udp_transport.h>
#include <packet_router.h>
#include <map>

namespace sprot {

class Session_Manager::Session_Manager_Implementation
{
    private:

        struct Transport_Tuple
        {
            Extended_Transport_Interface* l0_transport_ = nullptr;
            Packet_Router* l1_transport_ = nullptr;

            bool is_valid(){ return ((l0_transport_ != nullptr) && (l1_transport_ != nullptr)); }
        };

        std::map<Params, Transport_Tuple> transports_;

        std::recursive_mutex transports_mutex_;

        Transport_Tuple find_or_create(const Params& local_config);


    public:

        Session_Manager_Implementation() {}
        ~Session_Manager_Implementation() {}

        Session* connect(const Params& local_config, const Address& remote, size_t timeout);
        Session* accept(const Params& local_config, Address& remote, size_t timeout);
};

Session_Manager::Session_Manager_Implementation::Transport_Tuple
Session_Manager::Session_Manager_Implementation::find_or_create(const Params& local_config)
{
    Transport_Tuple tuple;

    std::lock_guard<std::recursive_mutex> lock(transports_mutex_);

    auto tp(transports_.find(local_config));
    if (tp != transports_.end())
        tuple = tp->second;
    else
    {
        //here we can potentially select transports based on params,
        //meaning not only UDP but maybe COM/USB, however for now only UDP is implemented

        try
        {
            Udp_Transport* t = new Udp_Transport();
            t->enable(local_config);

            tuple.l0_transport_ = t;
            tuple.l1_transport_ = new Packet_Router(tuple.l0_transport_);

            transports_[local_config] = tuple;

        }
        catch (...)
        {
            //TODO: handle this situation better
            delete tuple.l0_transport_;
            delete tuple.l1_transport_;

            Transport_Tuple empty;
            return empty;
        }
    }

    return tuple;
}

Session* Session_Manager::Session_Manager_Implementation::connect(const Params& local_config, const Address& remote, size_t timeout)
{
    Transport_Tuple tuple = find_or_create(local_config);

    if (!tuple.is_valid())
        return nullptr;

    Session* s = nullptr;

    try
    {
        s = new Session(tuple.l1_transport_);
        s->connect(local_config, remote, timeout);
    }
    catch (...)
    {
        delete s;
        return nullptr;
    }

    return s;
}

Session* Session_Manager::Session_Manager_Implementation::accept(const Params& local_config, Address& remote, size_t timeout)
{
    Transport_Tuple tuple = find_or_create(local_config);

    if (!tuple.is_valid())
        return nullptr;

    Session* s = nullptr;

    try
    {
        s = new Session(tuple.l1_transport_);
        s->accept(local_config, remote, timeout);
    }
    catch (...)
    {
        delete s;
        return nullptr;
    }

    return s;
}


Session* Session_Manager::connect(const Params& local_config, const Address& remote, size_t timeout)
{
    return impl_->connect(local_config, remote, timeout);
}

Session* Session_Manager::accept(const Params& local_config, Address& remote, size_t timeout)
{
    return impl_->accept(local_config, remote, timeout);
}

Session_Manager::Session_Manager()
{
    impl_ = new Session_Manager_Implementation();
}

Session_Manager::~Session_Manager()
{
    delete impl_;
}

};
