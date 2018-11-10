#include "l1_transport.h"
#include <fplog_exceptions.h>

namespace sprot
{

L1_transport::L1_transport(Extended_Transport_Interface* l0_transport):
l0_transport_(l0_transport),
read_bytes_(0)
{
}

size_t L1_transport::internal_read(void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout)
{//this method should be executed based on condition variable from a separate thread
    std::lock_guard lock(read_buffer_mutex_);
    size_t len = l0_transport_->read(buf, buf_size, user_data, timeout);

    if (len < sizeof(implementation::Frame))
        THROWM(fplog::exceptions::Read_Failed, "L1 received too little packet data - less than frame header.");
    if(user_data.size() < 2)
        THROWM(fplog::exceptions::Incorrect_Parameter, "L0 read did not return enough information about origin.");

    unsigned short port = 0;
    try
    {
        port = std::any_cast<unsigned short>(user_data[1]);
    }
    catch (std::bad_cast&)
    {
        THROWM(fplog::exceptions::Incorrect_Parameter, "Provided extended data contains unexpected data.");
    }

    implementation::Frame frame;
    memcpy(frame.bytes, buf, sizeof(implementation::Frame));

    user_data[1] = port;
    return len;
}

size_t L1_transport::schedule_read(Extended_Data& user_data, size_t timeout)
{
    std::condition_variable wait;
    std::unique_lock lock(waitlist_mutex_);

    if (waitlist_.find(user_data) != waitlist_.end())
    {
        if (waitlist_[user_data]->wait_for(lock, std::chrono::milliseconds(timeout)) == std::cv_status::timeout)
            THROW(fplog::exceptions::Timeout);
    }

    waitlist_[user_data] = &wait;
    read_signal_.notify_all();

    if (wait.wait_for(lock, std::chrono::milliseconds(timeout)) == std::cv_status::timeout)
    {
        waitlist_[user_data] = nullptr;
        THROW(fplog::exceptions::Timeout);
    }

    return read_bytes_;
}

size_t L1_transport::read(void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout)
{
    if (!l0_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    if (null_data(user_data))
        THROWM(fplog::exceptions::Incorrect_Parameter, "Extended data cannot be missing, calling L1 read.");

    if (!buf)
        THROWM(fplog::exceptions::Incorrect_Parameter, "Buffer for storing data cannot be missing calling read.");

    if (buf_size < sizeof (implementation::Max_Frame_Size))
        THROWM(fplog::exceptions::Incorrect_Parameter, "Buffer for storing data is too small.");

    size_t len = schedule_read(user_data, timeout);

    std::lock_guard lock(read_buffer_mutex_);
    memcpy(buf, read_buffer_, len);

    return len;
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
