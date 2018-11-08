#include "l1_transport.h"
#include <fplog_exceptions.h>

namespace sprot
{

L1_transport::L1_transport(Extended_Transport_Interface* l0_transport):
l0_transport_(l0_transport)
{
}

size_t L1_transport::read(void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout)
{
    if (!l0_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    size_t len = l0_transport_->read(buf, buf_size, user_data, timeout);

}

size_t L1_transport::write(const void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout)
{
    if (!l0_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    return 0;
}

L1_transport::~L1_transport()
{
}

};
