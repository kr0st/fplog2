#include "packet_router.h"
#include <fplog_exceptions.h>
#include <chrono>

namespace sprot
{

void Packet_Router::reader_thread(Packet_Router* p)
{
    unsigned char read_buffer[sprot::implementation::Max_Frame_Size];

    while (!p->stop_reading_)
    {
        Address read_ext_data;
        Read_Request* req(nullptr);

        try
        {
            unsigned long read_bytes = p->l0_transport_->read(read_buffer, sizeof(implementation::Frame::bytes), read_ext_data, 250);

            if (read_bytes < sizeof(implementation::Frame::bytes))
            {
                debug_logging::g_logger.log("read_bytes < sizeof(implementation::Frame::bytes)\n");
                continue;
            }

            implementation::Frame frame;
            memcpy(frame.bytes, read_buffer, sizeof(implementation::Frame::bytes));

            if (((frame.details.type == implementation::Frame_Type::Data_Frame) ||
                 (frame.details.type == implementation::Frame_Type::Retransmit_Frame)) &&
                    (frame.details.data_len > 0) && (frame.details.data_len <= implementation::Mtu))
            {
                read_bytes += p->l0_transport_->read(&(read_buffer[sizeof(implementation::Frame::bytes)]), frame.details.data_len, read_ext_data, 250);
                if (read_bytes != (sizeof(implementation::Frame::bytes) + frame.details.data_len))
                {
                    debug_logging::g_logger.log("read_bytes != (sizeof(implementation::Frame::bytes) + frame.details.data_len)\n");
                    continue;
                }
            }

            if (!implementation::crc_check(read_buffer, read_bytes))
            {
                debug_logging::g_logger.log("!implementation::crc_check(read_buffer, read_bytes)\n");
                continue;
            }

            read_ext_data.port = frame.details.origin_listen_port;

            Address tuple(read_ext_data);

            {
                if (p->stop_reading_)
                    return;

                std::lock_guard lock(p->waitlist_mutex_);

                auto q_iter = p->waitlist_.find(tuple);
                if (q_iter != p->waitlist_.end())
                {
                    std::vector<Read_Request*> q(q_iter->second);
                    if (q.size() == 0)
                    {
                        req = new Read_Request();
                        q.push_back(req);
                    }
                    else
                    {
                        auto it(q.begin());

                        for (; it != q.end(); ++it)
                        {
                            if ((*it) && !(*it)->done)
                            {
                                req = *it;
                                break;
                            }
                        }

                        if (it == q.end())
                        {
                            req = new Read_Request();
                            q.push_back(req);
                        }
                    }
                }
                else
                {
                    Address empty_tuple;
                    q_iter = p->waitlist_.find(empty_tuple);

                    tuple = empty_tuple;

                    if (q_iter != p->waitlist_.end())
                    {
                        std::vector<Read_Request*> q(q_iter->second);
                        if (q.size() == 0)
                        {
                            req = new Read_Request();
                            q.push_back(req);
                        }
                        else
                        {
                            auto it(q.begin());

                            for (; it != q.end(); ++it)
                            {
                                if ((*it) && !(*it)->done)
                                {
                                    req = *it;
                                    break;
                                }
                            }

                            if (it == q.end())
                            {
                                req = new Read_Request();
                                q.push_back(req);
                            }
                        }
                    }
                    else
                    {
                        req = new Read_Request();
                        std::vector <Read_Request*> q;
                        q.push_back(req);
                        p->waitlist_[tuple] = q;
                    }
                }

                req->read_bytes = read_bytes;
                memcpy(req->read_buffer, read_buffer, read_bytes);
                req->read_ext_data = read_ext_data;

                req->done = true;
            }
        }
        catch (std::exception&)
        {
            continue;
        }
        catch (fplog::exceptions::Generic_Exception&)
        {
            continue;
        }
    }
}

Packet_Router::Packet_Router(Extended_Transport_Interface* l0_transport):
l0_transport_(l0_transport),
stop_reading_(false),
reader_(std::bind(Packet_Router::reader_thread, this))
{
}

Packet_Router::Read_Request Packet_Router::schedule_read(Address& user_data, size_t timeout)
{   
    Read_Request* req;
    size_t req_num;
    std::vector<Read_Request*>* queue;

    Address tuple(user_data);

    auto timer_start = sprot::implementation::check_time_out(timeout);

    {
        std::lock_guard lock(waitlist_mutex_);
        std::map<Address, std::vector<Read_Request*>>::iterator res(waitlist_.find(tuple));

        if (res != waitlist_.end())
        {
            if (res->second.size() == 0)
            {
                req = new Read_Request();
                queue = &res->second;
                res->second.push_back(req);
                req_num = res->second.size() - 1;
            }
            else
            {
                for (req_num = 0; req_num < res->second.size(); req_num++)
                    if ((res->second[req_num]) && (res->second[req_num]->done))
                    {
                        req = res->second[req_num];
                        queue = &res->second;
                        break;
                    }

                if (req_num == res->second.size())
                {
                    for (req_num = 0; req_num < res->second.size(); req_num++)
                        if ((res->second[req_num]) && (!res->second[req_num]->done))
                        {
                            req = res->second[req_num];
                            queue = &res->second;
                            break;
                        }

                    if (req_num == res->second.size())
                    {
                        //queue has only deleted requests, clear it completely
                        req = new Read_Request();
                        queue = &res->second;
                        res->second.push_back(req);
                        req_num = res->second.size() - 1;
                    }
                }
            }
        }
        else
        {
            req = new Read_Request();

            std::vector<Read_Request*> q;
            q.push_back(req);

            waitlist_[tuple] = q;
            req_num = 0;

            queue = &waitlist_[tuple];
        }
    }

    try
    {
        while (!req->done)
        {
            implementation::check_time_out(timeout, timer_start);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        Read_Request result(*req);

        std::lock_guard lock(waitlist_mutex_);

        delete req;
        (*queue)[req_num] = nullptr;

        return result;
    }
    catch (fplog::exceptions::Timeout&)
    {
    }

    return Read_Request();
}

size_t Packet_Router::read(void* buf, size_t buf_size, Address& user_data, size_t timeout)
{
    if (!l0_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    if (null_data(user_data))
        THROWM(fplog::exceptions::Incorrect_Parameter, "Extended data cannot be missing, calling L1 read.");

    if (!buf)
        THROWM(fplog::exceptions::Incorrect_Parameter, "Buffer for storing data cannot be missing calling read.");

    if (buf_size < sizeof(implementation::Frame::bytes))
        THROWM(fplog::exceptions::Incorrect_Parameter, "Buffer for storing data is too small.");

    Read_Request req(schedule_read(user_data, timeout));

    memcpy(buf, req.read_buffer, req.read_bytes);

    user_data.ip = req.read_ext_data.ip;
    user_data.port = req.read_ext_data.port;

    return req.read_bytes;
}

size_t Packet_Router::write(const void* buf, size_t buf_size, Address& user_data, size_t timeout)
{
    if (buf_size == 0)
        return 0;

    if (!l0_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    if (null_data(user_data))
        THROWM(fplog::exceptions::Incorrect_Parameter, "Extended data cannot be missing, calling write.");

    if (!buf)
        THROWM(fplog::exceptions::Incorrect_Parameter, "Buffer for sending data cannot be missing calling write.");

    return l0_transport_->write(buf, buf_size, user_data, timeout);
}

Packet_Router::~Packet_Router()
{
    stop_reading_ = true;
    reader_.join();
}

};
