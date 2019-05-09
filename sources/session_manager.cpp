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
            Extended_Transport_Interface* l0_transport_;
            Packet_Router* l1_transport_;
        };

        std::map<Params, Transport_Tuple> transports_;


    public:

        Session_Manager_Implementation() {}
        ~Session_Manager_Implementation() {}
};



Session* Session_Manager::connect(const Params& local_config, const Params& remote, size_t timeout)
{
    return nullptr;
}

Session* Session_Manager::accept(const Params& local_config, const Params& remote, size_t timeout)
{
    return nullptr;
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
