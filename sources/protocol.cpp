#include <sprot.h>

namespace sprot { namespace implementation {

class Protocol: Basic_Transport_Interface
{
    public:

        size_t read(void* buf, size_t buf_size, size_t timeout = infinite_wait);
        size_t write(const void* buf, size_t buf_size, size_t timeout = infinite_wait);

        bool connect(Protocol_Interface::Params params, size_t timeout = infinite_wait);
        bool accept(Protocol_Interface::Params params, size_t timeout = infinite_wait);

        Protocol(Extended_Transport_Interface* l1_transport);
};



}}
