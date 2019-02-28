
#include <protocol.h>

namespace sprot { namespace implementation {

Frame frame_from_buffer(void* buffer)
{
    Frame frame;
    memcpy(frame.bytes, buffer, sizeof(frame.bytes));
    return frame;
}

class Frame_Logger
{
    public:

        Frame_Logger()
        {
            unsigned i = 0x13;
            frame_types_[i++] = "HS";
            frame_types_[i++] = "GB";
            frame_types_[i++] = "AK";
            frame_types_[i++] = "NA";
            frame_types_[i++] = "DT";
            frame_types_[i++] = "RR";
        }

        void log(void* buf, const char* direction)
        {
            if (!buf || !direction)
                return;

            Frame frame(frame_from_buffer(buf));
            bool crc = crc_check(buf, sizeof(Frame::bytes) + frame.details.data_len);

            char str[1024];
            char host[21];
            char data[255];

            memset(data, 0, sizeof(data));
            memcpy(data, static_cast<char*>(buf) + sizeof(frame.bytes), frame.details.data_len);

            memset(host, 0, sizeof(host));
            memcpy(host, frame.details.hostname, sizeof(frame.details.hostname));

            std::string type;
            auto it = frame_types_.find(frame.details.type);
            if (it != frame_types_.end())
                type = it->second;

            sprintf(str, "ln#%10lu: %s %s origin ip=%u.%u.%u.%u listen port=%5u hostname=%21s seq=%10u crc=%s data_sz=%u data=%s\n",
                    ln_++,
                    direction, type.c_str(), reinterpret_cast<unsigned char*>(&(frame.details.origin_ip))[0],
                    reinterpret_cast<unsigned char*>(&(frame.details.origin_ip))[1],
                    reinterpret_cast<unsigned char*>(&(frame.details.origin_ip))[2],
                    reinterpret_cast<unsigned char*>(&(frame.details.origin_ip))[3],
                    frame.details.origin_listen_port, host, frame.details.sequence,
                    crc ? "ok " : "nok", frame.details.data_len, data);

            debug_logging::g_logger.log(str);
        }


    private:

        std::map<unsigned, std::string> frame_types_;
        long unsigned int ln_ = 0;
};

static Frame_Logger logger;

void Protocol::trim_storage(std::map<unsigned int, Packed_Buffer>& storage)
{
    if (storage.size() >= storage_max_)
    {
        if (storage_trim_ >= storage.size())
            storage.clear();

        auto start = storage.begin();
        auto finish = start;
        for (unsigned int i = 0; i < storage_trim_; i++, finish++);

        storage.erase(start, finish);
    }
}

void Protocol::empty_storage(std::map<unsigned int, Packed_Buffer>& storage)
{
    std::map<unsigned int, Packed_Buffer> empty;
    storage.clear();
    storage.swap(empty);
}

void Protocol::put_in_storage(std::map<unsigned int, Packed_Buffer>& storage, unsigned int sequence, const void* buf)
{
    Packed_Buffer packed;
    memset(packed.buffer, 0, Max_Frame_Size);
    memcpy(packed.buffer, buf, Max_Frame_Size);
    storage[sequence] = packed;
}

void Protocol::take_from_storage(std::map<unsigned int, Packed_Buffer>& storage, unsigned int sequence, void* buf)
{
    memset(buf, 0, Max_Frame_Size);
    auto frame = storage.find(sequence);
    if (frame != storage.end())
        memcpy(buf, frame->second.buffer, Max_Frame_Size);
}

bool is_buffer_empty(const void* buffer)
{
    char empty[Max_Frame_Size];
    memset(empty, 0, Max_Frame_Size);
    if (memcmp(buffer, empty, Max_Frame_Size) == 0)
        return true;
    else
        return false;
}

Frame_Type frame_type(void* buffer)
{
    if (buffer)
        return Frame_Type(frame_from_buffer(buffer).details.type);

    return Frame_Type::Unknown_Frame;
}

Frame Protocol::make_frame(Frame_Type type, size_t data_len, const void* data)
{
    if (type > Frame_Type::Unknown_Frame)
        THROW1(exceptions::Unknown_Frame, type);

    Frame frame;

    if (type == Frame_Type::Handshake_Frame)
    {
        send_sequence_ = 0;
        recv_sequence_ = 0;
    }

    frame.details.type = type;
    frame.details.data_len = static_cast<unsigned short>(data_len);

    if (send_sequence_ == UINT32_MAX)
    {
        if (frame.details.type == Frame_Type::Data_Frame)
        {
            frame.details.sequence = send_sequence_;
            send_sequence_ = 0;
        }
    }
    else
    {
        if (frame.details.type == Frame_Type::Data_Frame)
            frame.details.sequence = send_sequence_++;
    }

    if (frame.details.type != Frame_Type::Data_Frame)
    {
        frame.details.sequence = 0;
    }

    frame.details.origin_ip = local_.ip;
    frame.details.origin_listen_port = local_.port;

    memcpy(frame.details.hostname, localhost_, sizeof(frame.details.hostname));

    memcpy(write_buffer_, frame.bytes, sizeof(frame.bytes));

    if (data && (data_len > 0))
        memcpy(write_buffer_ + sizeof(frame.bytes), data, data_len);

    unsigned short crc = generic_util::gen_crc16(write_buffer_ + 2, static_cast<unsigned short>(sizeof(frame.bytes) + data_len) - 2);
    unsigned short *pcrc = reinterpret_cast<unsigned short*>(&write_buffer_[0]);
    *pcrc = crc;

    if (frame.details.type == Frame_Type::Data_Frame)
    {
        trim_storage(stored_writes_);
        put_in_storage(stored_writes_, frame.details.sequence, write_buffer_);
    }

    return frame;
}

void Protocol::send_frame(size_t timeout)
{
    if (!l1_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    Frame frame;
    memcpy(frame.bytes, write_buffer_, sizeof(frame.bytes));
    size_t expected_bytes = frame.details.data_len + sizeof(frame.bytes);

    logger.log(write_buffer_, "->");

    size_t sent_bytes = l1_transport_->write(write_buffer_, expected_bytes, remote_, timeout);
    if (sent_bytes != expected_bytes)
        THROW2(exceptions::Size_Mismatch, expected_bytes, sent_bytes);
}

Frame Protocol::receive_frame(size_t timeout)
{
    if (!l1_transport_)
        THROW(fplog::exceptions::Transport_Missing);

recv_again:

    size_t received_bytes = l1_transport_->read(read_buffer_, Max_Frame_Size, remote_, timeout);

    Frame frame;
    memcpy(frame.bytes, read_buffer_, sizeof(frame.bytes));

    size_t expected_bytes = frame.details.data_len + sizeof(frame.bytes);

    if (received_bytes != expected_bytes)
        THROW2(exceptions::Size_Mismatch, expected_bytes, received_bytes);

    logger.log(read_buffer_, "<-");

    unsigned short crc_expected, crc_actual;
    if (!crc_check(read_buffer_, expected_bytes, &crc_expected, &crc_actual))
        THROW2(exceptions::Crc_Check_Failed, crc_expected, crc_actual);

    if (frame.details.type == Frame_Type::Data_Frame)
    {
        trim_storage(stored_reads_);

        if (stored_reads_.find(frame.details.sequence) != stored_reads_.end())
        {
            goto recv_again;
        }

        put_in_storage(stored_reads_, frame.details.sequence, read_buffer_);

        if (frame.details.sequence == recv_sequence_)
        {
            if (recv_sequence_ == UINT32_MAX)
                recv_sequence_ = 0;
            else
                recv_sequence_++;
        }
    }

    return frame;
}

bool Protocol::connect(const Params& local_config, Address remote, size_t timeout)
{
    std::lock_guard lock(mutex_);

    local_.from_params(local_config);
    remote_ = remote;

    memset(localhost_, 0, sizeof(localhost_));

    for (auto& param : local_config)
    {
        if (generic_util::find_str_no_case(param.first, "hostname"))
        {
            std::string host(param.second);
            host = generic_util::trim(host);
            size_t max_copy = host.length() > sizeof(localhost_) ? sizeof(localhost_) : host.length();
            memcpy(localhost_, host.c_str(), max_copy);
        }
    }

    fplog::exceptions::Generic_Exception last_known_exception;
    bool exception_happened = false;

    auto main_timeout_timer = check_time_out(timeout);

    auto send_handshake = [&]() -> bool
    {
        check_time_out(timeout, main_timeout_timer);

        try
        {
            make_frame(Frame_Type::Handshake_Frame);
            send_frame(op_timeout_);

            Frame frame(receive_frame(op_timeout_));
            if (frame.details.type != Frame_Type::Ack_Frame)
                return false;

            empty_storage(stored_writes_);
            empty_storage(stored_reads_);

            return true;
        }
        catch (fplog::exceptions::Timeout&)
        {
            //cheking t/o again because we might have op timeout
            //but not t/o used as argument in connect
            //in case we have t/o from argument, t/o exception will be thrown again
            check_time_out(timeout, main_timeout_timer);
            return false;
        }
        catch (fplog::exceptions::Generic_Exception& e)
        {
            exception_happened = true;
            last_known_exception = e;
            return false;
        }
        catch (...)
        {
            return false;
        }
    };

    generic_util::Retryable handshake(send_handshake, max_retries_);

    connected_ = handshake.run();
    acceptor_ = false;

    return connected_;
}

bool Protocol::accept(const Params& local_config, Address remote, size_t timeout)
{
    std::lock_guard lock(mutex_);

    local_.from_params(local_config);
    remote_ = remote;

    memset(localhost_, 0, sizeof(localhost_));

    for (auto& param : local_config)
    {
        if (generic_util::find_str_no_case(param.first, "hostname"))
        {
            std::string host(param.second);
            host = generic_util::trim(host);
            size_t max_copy = host.length() > sizeof(localhost_) ? sizeof(localhost_) : host.length();
            memcpy(localhost_, host.c_str(), max_copy);
        }
    }

    fplog::exceptions::Generic_Exception last_known_exception;
    bool exception_happened = false;

    auto main_timeout_timer = check_time_out(timeout);

    auto recv_handshake = [&]() -> bool
    {
        check_time_out(timeout, main_timeout_timer);

        try
        {
            Frame frame(receive_frame(op_timeout_));
            if (frame.details.type != Frame_Type::Handshake_Frame)
                return false;

            make_frame(Frame_Type::Ack_Frame);
            send_frame(op_timeout_);

            empty_storage(stored_writes_);
            empty_storage(stored_reads_);

            return true;
        }
        catch (fplog::exceptions::Timeout&)
        {
            //cheking t/o again because we might have op timeout
            //but not t/o used as argument in connect
            //in case we have t/o from argument, t/o exception will be thrown again
            check_time_out(timeout, main_timeout_timer);
            return false;
        }
        catch (fplog::exceptions::Generic_Exception& e)
        {
            exception_happened = true;
            last_known_exception = e;
            return false;
        }
        catch (...)
        {
            return false;
        }
    };

    generic_util::Retryable handshake(recv_handshake, max_retries_);
    connected_ = handshake.run();
    acceptor_ = true;

    return connected_;
}

size_t Protocol::read(void* buf, size_t buf_size, size_t timeout)
{
    if (!buf || (buf_size == 0))
        return 0;

    if (buf_size < sprot::implementation::Mtu)
        THROW(fplog::exceptions::Buffer_Overflow);

    std::lock_guard lock(mutex_);

    if (!connected_)
        THROW(fplog::exceptions::Not_Connected);

    auto process_recovered = [&]() -> unsigned int
    {
        if (!recovered_frames_.empty())
        {
            unsigned int sequence = recovered_frames_.front();
            recovered_frames_.pop();

            take_from_storage(stored_reads_, sequence, read_buffer_);
            Frame frame(frame_from_buffer(read_buffer_));

            memcpy(buf, read_buffer_ + sizeof(frame.bytes), frame.details.data_len);

            if (recovered_frames_.empty())
            {
                if (frame.details.sequence == UINT32_MAX)
                    recv_sequence_ = 0;
                else
                    recv_sequence_ = frame.details.sequence + 1;
            }

            return frame.details.data_len;
        }
        else
            return 0;
    };

    unsigned int recovered_len = process_recovered();
    if (recovered_len > 0)
        return recovered_len;

    fplog::exceptions::Generic_Exception last_known_exception;
    bool exception_happened = false;
    Frame frame;

    auto main_timeout_timer = check_time_out(timeout);

    auto read_data = [&]() -> bool
    {
        check_time_out(timeout, main_timeout_timer);

        try
        {
            frame = receive_frame(op_timeout_);
            if (frame.details.type != Frame_Type::Data_Frame)
                return false;

            if (((frame.details.sequence + 1) == recv_sequence_) ||
                ((frame.details.sequence == UINT32_MAX) && (recv_sequence_ == 0)))
            {
                //Correct sequence number, can return with success.
                if ((frame.details.sequence % this->no_ack_count_) == 0)
                {
                    make_frame(Frame_Type::Ack_Frame);

                    //sending double ack for increased stability
                    send_frame(op_timeout_);
                    send_frame(op_timeout_);
                }

                memcpy(buf, read_buffer_ + sizeof(frame.bytes), frame.details.data_len);
                return true;
            }
            else
                THROW2(sprot::exceptions::Wrong_Number, recv_sequence_, (frame.details.sequence + 1));
        }
        catch (fplog::exceptions::Timeout&)
        {
            //cheking t/o again because we might have op timeout
            //but not t/o used as argument in connect
            //in case we have t/o from argument, t/o exception will be thrown again
            check_time_out(timeout, main_timeout_timer);
            return false;
        }
        catch (sprot::exceptions::Wrong_Number& e)
        {
            throw e;
        }
        catch (fplog::exceptions::Generic_Exception& e)
        {
            exception_happened = true;
            last_known_exception = e;
            return false;
        }
        catch (...)
        {
            return false;
        }
    };

    try
    {
        generic_util::Retryable transmission(read_data, max_retries_);
        bool success(transmission.run());

        if (success)
            return frame.details.data_len;
        else
        {
            if (exception_happened)
                throw last_known_exception;
            else
                return 0;
        }
    }
    catch (sprot::exceptions::Wrong_Number&)
    {
        if (retransmit_request(main_timeout_timer, timeout, frame.details.sequence))
        {
            size_t len = process_recovered();
            if (len > 0)
                return len;
        }

        connected_ = false;
        THROW(exceptions::Connection_Broken);
    }
}

bool Protocol::retransmit_request(std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration> timer_start,
                                  size_t timeout,
                                  unsigned int last_received_sequence)
{
    Frame frame;
    bool any_data = false;
    unsigned int stored_seq = recv_sequence_;

    bool expect_ack = false;

    auto read_data = [&]() -> bool
    {
        check_time_out(timeout, timer_start);

read_again:

        try
        {
            frame = receive_frame(op_timeout_);

            if (expect_ack)
            {
                if (frame.details.type == Frame_Type::Ack_Frame)
                {
                    expect_ack = false;
                    goto read_again;
                }
            }

            if (frame.details.type != Frame_Type::Data_Frame)
                return false;
            else
                last_received_sequence = frame.details.sequence;

            if (!any_data)
            {
                if ((last_received_sequence % no_ack_count_) == 0)
                    return true;
            }
            else
                return  true;

            return false;
        }
        catch (fplog::exceptions::Timeout&)
        {
            //cheking t/o again because we might have op timeout
            //but not t/o used as argument in connect
            //in case we have t/o from argument, t/o exception will be thrown again
            check_time_out(timeout, timer_start);
            return false;
        }
        catch (...)
        {
            return false;
        }
    };

    unsigned int read_failures = 0;
    while ((last_received_sequence % no_ack_count_) != 0)
    {
        if (!read_data())
            read_failures++;
        if (read_failures >= max_retries_)
            break;
    }

    std::vector <unsigned int> missing;
    for (unsigned int seq = recv_sequence_; seq != last_received_sequence; )
    {
        auto found_frame = stored_reads_.find(seq);
        if (found_frame == stored_reads_.end())
            missing.push_back(seq);

        if (seq == UINT32_MAX)
            seq = 0;
        else
            seq++;
    }

    bool send_ack = missing.empty();
    unsigned rr_from = 0, rr_count = 0;

    auto send_ack_or_retransmit = [&]() -> bool
    {
        check_time_out(timeout, timer_start);

        try
        {
            if (send_ack)
            {
                make_frame(Frame_Type::Ack_Frame);

                //sending double ack for increased stability
                send_frame(op_timeout_);
            }
            else
                make_frame(Frame_Type::Retransmit_Frame, rr_count * sizeof(unsigned int), &(missing[rr_from]));

            send_frame(op_timeout_);

            if (!send_ack)
            {
                Frame ack = receive_frame(op_timeout_);
                if (ack.details.type != Frame_Type::Ack_Frame)
                    return false;
            }

            return true;
        }
        catch (fplog::exceptions::Timeout&)
        {
            //cheking t/o again because we might have op timeout
            //but not t/o used as argument in connect
            //in case we have t/o from argument, t/o exception will be thrown again
            check_time_out(timeout, timer_start);
            return false;
        }
        catch (sprot::exceptions::Wrong_Number& e)
        {
            throw e;
        }
        catch (...)
        {
            return false;
        }
    };

    generic_util::Retryable ack_or_rr(send_ack_or_retransmit, max_retries_);

    if (send_ack)
        ack_or_rr.run();

    rr_from = 0;
    rr_count = static_cast<unsigned>(missing.size());

    while (rr_from < missing.size())
    {
        if (!send_ack)
        {
            while (rr_count * sizeof(unsigned int) > Mtu)
            {
                if (rr_count == 0)
                    break;
                rr_count--;
            }

            if(!ack_or_rr.run())
                return false; //retransmit failed
        }

        if (!missing.empty())
        {
            expect_ack = true;

            generic_util::Retryable receive_missing(read_data, max_retries_);
            any_data = true;

            for (unsigned i = 0; i < rr_count; ++i)
                receive_missing.run();

            rr_from += rr_count;
            unsigned temp = rr_count;
            rr_count = static_cast<unsigned>(missing.size()) - temp;
        }
        else
            break;
    }


    {
        std::queue<unsigned int> empty;
        recovered_frames_.swap(empty);
    }

    if ((stored_seq == last_received_sequence) && !missing.empty())
    {
        missing.clear();

        if (stored_reads_.find(stored_seq) == stored_reads_.end())
            missing.push_back(stored_seq);
        else
            recovered_frames_.push(stored_seq);
    }
    else
        missing.clear();

    for (unsigned int seq = stored_seq; seq != last_received_sequence; )
    {
        if (stored_reads_.find(seq) == stored_reads_.end())
            missing.push_back(seq);
        else
            recovered_frames_.push(seq);

        if (seq == UINT32_MAX)
            seq = 0;
        else
            seq++;
    }

    if (missing.empty() && (!send_ack))
    {
        send_ack = true;
        ack_or_rr.run();

        return true;
    }

    return missing.empty();
}

size_t Protocol::write(const void* buf, size_t buf_size, size_t timeout)
{
    if (!buf || (buf_size == 0))
        return 0;

    if (buf_size > sprot::implementation::Mtu)
        THROW(fplog::exceptions::Buffer_Overflow);

    std::lock_guard lock(mutex_);

    if (!connected_)
        THROW(fplog::exceptions::Not_Connected);

    fplog::exceptions::Generic_Exception last_known_exception;
    bool exception_happened = false;

    auto main_timeout_timer = check_time_out(timeout);

    Frame data(make_frame(Frame_Type::Data_Frame, buf_size, buf));
    Frame recv_frame;

    auto send_data = [&]() -> bool
    {
        check_time_out(timeout, main_timeout_timer);

        try
        {
            take_from_storage(stored_writes_, data.details.sequence, write_buffer_);
            send_frame(op_timeout_);

            if ((data.details.sequence % this->no_ack_count_) == 0)
            {
                recv_frame = receive_frame(op_timeout_);
                if (recv_frame.details.type != Frame_Type::Ack_Frame)
                    THROW2(exceptions::Unexpected_Frame, Frame_Type::Ack_Frame, recv_frame.details.type);
            }

            return true;
        }
        catch (fplog::exceptions::Timeout&)
        {
            //cheking t/o again because we might have op timeout
            //but not t/o used as argument in connect
            //in case we have t/o from argument, t/o exception will be thrown again
            check_time_out(timeout, main_timeout_timer);
            return false;
        }
        catch (exceptions::Unexpected_Frame& e)
        {
            throw e;
        }
        catch (fplog::exceptions::Generic_Exception& e)
        {
            exception_happened = true;
            last_known_exception = e;
            return false;
        }
        catch (...)
        {
            return false;
        }
    };

    generic_util::Retryable transmission(send_data, max_retries_);
    bool success = false;

    try
    {
        success = transmission.run();

        if (success)
            return buf_size;
        else
        {
            if (exception_happened)
                throw last_known_exception;
            else
                return 0;
        }
    }
    catch (exceptions::Unexpected_Frame& e)
    {
        if (recv_frame.details.type != Frame_Type::Retransmit_Frame)
        {
            if (send_sequence_ == 0)
                send_sequence_ = UINT32_MAX;
            else
                send_sequence_--;

            throw e;
        }
        if (retransmit_response(main_timeout_timer, timeout))
            return buf_size;
        else
        {
            connected_ = false;
            THROW(exceptions::Connection_Broken);
        }
    }
    catch(fplog::exceptions::Timeout& e)
    {
        if (send_sequence_ == 0)
            send_sequence_ = UINT32_MAX;
        else
            send_sequence_--;

        throw e;
    }
    catch (...)
    {
        connected_ = false;
        THROW(exceptions::Connection_Broken);
    }
}

bool Protocol::retransmit_response(std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration> timer_start, size_t timeout)
{
retrans_again:

    Frame retransmit(frame_from_buffer(read_buffer_));

    if (retransmit.details.data_len < sizeof(unsigned int))
        return false;

    unsigned int count = retransmit.details.data_len / sizeof(unsigned int);
    if (count == 0)
        return false;

    std::vector<unsigned int> resend_frames;
    resend_frames.resize(count);

    memcpy(&(resend_frames[0]), read_buffer_ + sizeof(Frame::bytes), retransmit.details.data_len);

    unsigned char empty_buffer_[Max_Frame_Size];
    memset(empty_buffer_, 0, Max_Frame_Size);

    auto send_data = [&]() -> bool
    {
        check_time_out(timeout, timer_start);

        try
        {

            make_frame(Ack_Frame);
            send_frame(op_timeout_);

            for (size_t i = 0; i < resend_frames.size(); ++i)
            {
                take_from_storage(stored_writes_, resend_frames[i], write_buffer_);
                if (memcmp(write_buffer_, empty_buffer_, Max_Frame_Size) == 0)
                {
                    connected_ = false;
                    THROW(exceptions::Connection_Broken);
                }
                send_frame(op_timeout_);
            }

            Frame ack_rr(receive_frame(op_timeout_));

            if (ack_rr.details.type == Frame_Type::Ack_Frame)
                return true;

            if (ack_rr.details.type == Frame_Type::Retransmit_Frame)
                THROW(fplog::exceptions::Generic_Exception);

            return false;
        }
        catch (fplog::exceptions::Timeout&)
        {
            //cheking t/o again because we might have op timeout
            //but not t/o used as argument in connect
            //in case we have t/o from argument, t/o exception will be thrown again
            check_time_out(timeout, timer_start);
            return false;
        }
        catch (exceptions::Connection_Broken& e)
        {
            connected_ = false;
            throw e;
        }
        catch (exceptions::Repeat_Retransmit& e)
        {
            throw e;
        }
        catch (...)
        {
            return false;
        }
    };

    generic_util::Retryable retransmit_data(send_data, max_retries_);

    try
    {
        retransmit_data.run();
    }
    catch (exceptions::Repeat_Retransmit&)
    {
        goto retrans_again;
    }

    return true;
}

}}
