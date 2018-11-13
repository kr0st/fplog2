#include "packet_router.h"
#include <fplog_exceptions.h>
#include <chrono>

namespace sprot
{

void Packet_Router::reader_thread(Packet_Router* p)
{
    std::unique_lock lock(p->read_signal_mutex_);
    while (!p->stop_reading_)
    {
        std::cv_status to = p->read_signal_.wait_for(lock, std::chrono::milliseconds(500));
        if (to != std::cv_status::timeout)
        {
            std::lock_guard read_lock(p->read_buffer_mutex_);
            p->exception_happened_ = false;

            try
            {
                p->last_scheduled_read_ = nullptr;
                p->read_bytes_ = p->l0_transport_->read(p->read_buffer_, sizeof(p->read_buffer_), p->read_ext_data_, p->read_timeout_);
                if (p->read_ext_data_.size() < 2)
                    continue;

                Ip_Port tuple(p->read_ext_data_);

                if (p->waitlist_.find(tuple) != p->waitlist_.end())
                    p->last_scheduled_read_ = p->waitlist_[tuple];
                else
                {
                    Ip_Port empty_tuple;
                    if (p->waitlist_.find(empty_tuple) != p->waitlist_.end())
                        p->last_scheduled_read_ = p->waitlist_[empty_tuple];
                }

                if (p->read_bytes_ < sizeof(implementation::Frame))
                    THROWM(fplog::exceptions::Read_Failed, "L1 received too little packet data - less than frame header.");

                implementation::Frame frame;
                memcpy(frame.bytes, p->read_buffer_, sizeof(implementation::Frame));

                p->read_ext_data_[1] = static_cast<unsigned short>(frame.details.origin_listen_port);
                p->last_scheduled_read_->notify_all();
            }
            catch (fplog::exceptions::Generic_Exception& e)
            {
                p->exception_happened_ = true;
                p->read_exception_ = e;
                if (p->last_scheduled_read_)
                    p->last_scheduled_read_->notify_all();
            }
        }
    }
}

Packet_Router::Packet_Router(Extended_Transport_Interface* l0_transport):
l0_transport_(l0_transport),
read_bytes_(0),
reader_(std::bind(Packet_Router::reader_thread, this)),
stop_reading_(false)
{
}

size_t Packet_Router::schedule_read(Extended_Data& user_data, size_t timeout)
{
    std::condition_variable wait;
    std::unique_lock lock(waitlist_mutex_);

    Ip_Port tuple(user_data);

    if (waitlist_.find(tuple) != waitlist_.end())
    {
        if (waitlist_[tuple]->wait_for(lock, std::chrono::milliseconds(timeout)) == std::cv_status::timeout)
            THROW(fplog::exceptions::Timeout);
    }

    read_timeout_ = timeout;
    waitlist_[tuple] = &wait;
    read_signal_.notify_all();

    if (wait.wait_for(lock, std::chrono::milliseconds(timeout)) == std::cv_status::timeout)
    {
        waitlist_[tuple] = nullptr;
        THROW(fplog::exceptions::Timeout);
    }

    user_data.clear();
    for (auto it = read_ext_data_.begin(); it != read_ext_data_.end(); ++it)
        user_data.push_back(*it);

    return read_bytes_;
}

size_t Packet_Router::read(void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout)
{
    if (!l0_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    if (null_data(user_data))
        THROWM(fplog::exceptions::Incorrect_Parameter, "Extended data cannot be missing, calling L1 read.");

    if (!buf)
        THROWM(fplog::exceptions::Incorrect_Parameter, "Buffer for storing data cannot be missing calling read.");

    if (buf_size < sizeof (implementation::Max_Frame_Size))
        THROWM(fplog::exceptions::Incorrect_Parameter, "Buffer for storing data is too small.");

    read_bytes_ = 0;
    size_t len = schedule_read(user_data, timeout);

    std::lock_guard lock(read_buffer_mutex_);

    if (exception_happened_)
        throw read_exception_;

    memcpy(buf, read_buffer_, len);

    return len;
}

size_t Packet_Router::write(const void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout)
{
    if (buf_size == 0)
        return 0;

    if (!l0_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    if (null_data(user_data))
        THROWM(fplog::exceptions::Incorrect_Parameter, "Extended data cannot be missing, calling write.");

    if (!buf)
        THROWM(fplog::exceptions::Incorrect_Parameter, "Buffer for sending data cannot be missing calling write.");

    if(user_data.size() < 2)
        THROWM(fplog::exceptions::Incorrect_Parameter, "Not enough information about recipient.");

    return l0_transport_->write(buf, buf_size, user_data, timeout);
}

Packet_Router::~Packet_Router()
{
    stop_reading_ = true;
    reader_.join();
}

};
