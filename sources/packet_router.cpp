#include "packet_router.h"
#include <fplog_exceptions.h>
#include <chrono>
#include <memory>

namespace sprot
{

std::mutex Packet_Router::waitlist_trace_mutex_;

void Packet_Router::waste_management_thread(Packet_Router* p)
{
    while (!p->stop_reading_)
    {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        {
            std::lock_guard lock(p->waitlist_mutex_);

            debug_logging::g_logger.log("=== before GC cycle ===");
            p->waitlist_trace();

            auto q_iter = p->waitlist_.begin();

            unsigned int connections = 0;
            while (q_iter != p->waitlist_.end())
            {
                if (q_iter->second.empty())
                {
                    p->waitlist_.erase(q_iter);
                    q_iter = p->waitlist_.begin();
                }
                else
                {
                    connections++;

                    unsigned int requests = 0;

                    auto q = &q_iter->second;
                    auto req_iter = q->begin();

                    //dumb, fast and easy approach - just cut N latest requests that are over max_requests option
                    //could be updated to something smarter if needed
                    while (req_iter != q->end())
                    {
                        requests++;
                        if (requests > sprot::implementation::options.max_requests_in_queue)
                        {
                            delete *req_iter;
                            *req_iter = nullptr;

                            q->erase(req_iter);
                            req_iter = q->begin();
                            requests--;
                        }
                        else
                            req_iter++;
                    }

                    q_iter++;
                }
            }

            //now this is super dumb and easy - if too many connections - just delete everything and let
            //error correction mechanism (timeouts/retries) to kick in and restore useful active connections
            if (connections > sprot::implementation::options.max_connections)
            {
                for (auto it = p->waitlist_.begin(); it != p->waitlist_.end(); ++it)
                {
                    for (auto jit = it->second.begin(); jit != it->second.end(); ++jit)
                    {
                        delete *jit;
                        *jit = nullptr;
                    }

                    it->second.clear();
                }

                {
                    p->waitlist_.clear();
                    std::map<Address, std::vector<Read_Request*>> empty;
                    p->waitlist_.swap(empty);
                }
            }

            debug_logging::g_logger.log("=== after GC cycle ===");
            p->waitlist_trace();
        }
    }
}

void Packet_Router::reader_thread(Packet_Router* p)
{
    unsigned char* read_buffer = new unsigned char[implementation::options.max_frame_size];
    std::unique_ptr<unsigned char> pbuf(read_buffer);

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
                    (frame.details.data_len > 0) && (frame.details.data_len <= implementation::options.mtu))
            {
                read_bytes += p->l0_transport_->read(&(read_buffer[sizeof(implementation::Frame::bytes)]), frame.details.data_len, read_ext_data, 250);

                if (read_bytes != (sizeof(implementation::Frame::bytes) + frame.details.data_len))
                {
                    debug_logging::g_logger.log("read_bytes != (sizeof(implementation::Frame::bytes) + frame.details.data_len)\n");
                    continue;
                }

                if (!implementation::crc_check(read_buffer, read_bytes))
                {
                    debug_logging::g_logger.log("!implementation::crc_check(read_buffer, read_bytes)\n");
                    continue;
                }
            }
            else
            {
                if (!implementation::crc_check(read_buffer, read_bytes))
                {
                    debug_logging::g_logger.log("!implementation::crc_check(read_buffer, read_bytes)\n");
                    continue;
                }
            }

            read_ext_data.port = frame.details.origin_listen_port;

            Address tuple(read_ext_data);

            {
                if (p->stop_reading_)
                    return;

                std::lock_guard lock(p->waitlist_mutex_);

                //debug_logging::g_logger.log("=== before ===");
                //p->waitlist_trace();

                auto q_iter = p->waitlist_.find(tuple);

                if (q_iter != p->waitlist_.end())
                {
                    std::vector<Read_Request*>* q(&q_iter->second);
                    if (q->size() == 0)
                    {
                        req = new Read_Request();
                        q->push_back(req);
                    }
                    else
                    {
                        auto it(q->begin());

                        for (; it != q->end(); it++)
                        {
                            if ((*it) && !(*it)->done)
                            {
                                req = *it;
                                break;
                            }
                        }

                        if (it == q->end())
                        {
                            req = new Read_Request();
                            q->push_back(req);
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
                        std::vector<Read_Request*>* q(&q_iter->second);
                        if (q->size() == 0)
                        {
                            req = new Read_Request();
                            q->push_back(req);
                        }
                        else
                        {
                            auto it(q->begin());

                            for (; it != q->end(); ++it)
                            {
                                if ((*it) && !(*it)->done)
                                {
                                    req = *it;
                                    break;
                                }
                            }

                            if (it == q->end())
                            {
                                req = new Read_Request();
                                q->push_back(req);
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

                //debug_logging::g_logger.log("=== after ===");
                //p->waitlist_trace();
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
reader_(std::bind(Packet_Router::reader_thread, this)),
garbage_collector_(std::bind(Packet_Router::waste_management_thread, this))
{
}

Packet_Router::Read_Request Packet_Router::schedule_read(Address& user_data, size_t timeout)
{   
    Read_Request* req = nullptr;
    size_t req_num;
    std::vector<Read_Request*>* queue = nullptr;

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
                        queue->clear();
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
    garbage_collector_.join();
}

void Packet_Router::waitlist_trace()
{
    if (!debug_logging::g_logger.enabled())
        return;

    std::lock_guard lock(waitlist_trace_mutex_);

    std::string frame_types[0x13 + 7];

    unsigned i = 0x13;
    frame_types[i++] = "HS";
    frame_types[i++] = "GB";
    frame_types[i++] = "AK";
    frame_types[i++] = "NA";
    frame_types[i++] = "DT";
    frame_types[i++] = "RR";
    frame_types[i++] = "??";

    char str[255];

    sprintf(str, "Addresses in waitlist: %lu", waitlist_.size());
    debug_logging::g_logger.log("waitlist_ trace:");

    for (auto it(waitlist_.begin()); it != waitlist_.end(); it++)
    {
        sprintf(str, "ip = %x; port = %d; requests total (incl. del): %lu", it->first.ip, it->first.port, it->second.size());

        debug_logging::g_logger.log(str);

        for (auto req(it->second.begin()); req != it->second.end(); req++)
        {
            if ((*req) == nullptr)
            {
                debug_logging::g_logger.log("deleted request");
                continue;
            }

            if ((!(*req)->done) || ((*req)->read_bytes < 2))
                sprintf(str, "status: %d; length: %lu", (*req)->done, (*req)->read_bytes);

            if ((*req)->done && ((*req)->read_bytes >= 2))
            {
                unsigned short frame_type = 0;

                if (!(*req)->read_buffer)
                    frame_type = 0x13 + 6;
                else
                {
                    ((char*)&frame_type)[0] = (*req)->read_buffer[2];
                    ((char*)&frame_type)[1] = (*req)->read_buffer[3];
                }

                if (frame_type >= (0x13 + 6))
                    frame_type = 0x13 + 6;
                sprintf(str, "status: %d; length: %lu; frame type: %s; ip from: %x; port from (listen): %d", (*req)->done, (*req)->read_bytes,
                        frame_types[frame_type].c_str(), (*req)->read_ext_data.ip, (*req)->read_ext_data.port);
            }

            debug_logging::g_logger.log(str);
        }
    }
}

};
