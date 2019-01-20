
#include <protocol.h>

namespace sprot { namespace implementation {

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

Frame frame_from_buffer(void* buffer)
{
    Frame frame;
    memcpy(frame.bytes, buffer, sizeof(frame.bytes));
    return frame;
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
        frame.details.sequence = send_sequence_;
        send_sequence_ = 0;
    }
    else
    {
        if (frame.details.type == Frame_Type::Data_Frame)
            frame.details.sequence = send_sequence_++;
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

    return frame;
}

void Protocol::send_frame(size_t timeout)
{
    if (!l1_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    Frame frame;
    memcpy(frame.bytes, write_buffer_, sizeof(frame.bytes));
    size_t expected_bytes = frame.details.data_len + sizeof(frame.bytes);

    size_t sent_bytes = l1_transport_->write(write_buffer_, expected_bytes, remote_, timeout);
    if (sent_bytes != expected_bytes)
        THROW2(exceptions::Size_Mismatch, expected_bytes, sent_bytes);
}

Frame Protocol::receive_frame(size_t timeout)
{
    if (!l1_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    size_t received_bytes = l1_transport_->read(read_buffer_, Max_Frame_Size, remote_, timeout);

    Frame frame;
    memcpy(frame.bytes, read_buffer_, sizeof(frame.bytes));

    size_t expected_bytes = frame.details.data_len + sizeof(frame.bytes);

    if (received_bytes != expected_bytes)
        THROW2(exceptions::Size_Mismatch, expected_bytes, received_bytes);

    unsigned short crc_expected, crc_actual;
    if (!crc_check(read_buffer_, expected_bytes, &crc_expected, &crc_actual))
        THROW2(exceptions::Crc_Check_Failed, crc_expected, crc_actual);

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
    return handshake.run();
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
    return handshake.run();
}

size_t Protocol::read(void* buf, size_t buf_size, size_t timeout)
{
    return 0;
}

size_t Protocol::write(const void* buf, size_t buf_size, size_t timeout)
{
    if (!buf || (buf_size == 0))
        return 0;

    if (buf_size > sprot::implementation::Mtu)
        THROW(fplog::exceptions::Buffer_Overflow);

    std::lock_guard lock(mutex_);

    fplog::exceptions::Generic_Exception last_known_exception;
    bool exception_happened = false;

    auto main_timeout_timer = check_time_out(timeout);

    Frame data(make_frame(Frame_Type::Data_Frame, buf_size, buf));
    put_in_storage(stored_writes_, data.details.sequence, write_buffer_);

    auto send_data = [&]() -> bool
    {
        check_time_out(timeout, main_timeout_timer);

        try
        {
            take_from_storage(stored_writes_, data.details.sequence, write_buffer_);
            send_frame(op_timeout_);

            if ((send_sequence_ % this->no_ack_count_) == 0)
            {
                Frame frame(receive_frame(op_timeout_));
                if (frame.details.type != Frame_Type::Ack_Frame) //TODO: handle frame resending here
                    return false;
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
    bool success(transmission.run());

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

}}
