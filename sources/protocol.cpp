
#include <protocol.h>

namespace sprot { namespace implementation {

void Protocol::trim_storage()
{
    if (stored_writes_.size() >= storage_max_)
    {
        if (storage_trim_ >= stored_writes_.size())
            stored_writes_.clear();

        auto start = stored_writes_.begin();
        auto finish = start;
        for (unsigned int i = 0; i < storage_trim_; i++, finish++);

        stored_writes_.erase(start, finish);
    }
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

void Protocol::receive_frame(size_t timeout)
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

    if (frame.details.type != Frame_Type::Handshake_Frame)
    {
        if (frame.details.sequence != sequence_)
            THROW2(exceptions::Wrong_Number, sequence_, frame.details.sequence);

        if (sequence_ == UINT32_MAX)
            sequence_ = 0;
        else
            sequence_++;
    }
    else
        sequence_ = frame.details.sequence + 1;
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
        sequence_ = 0;

    frame.details.type = type;
    frame.details.data_len = static_cast<unsigned short>(data_len);

    if (sequence_ == UINT32_MAX)
    {
        frame.details.sequence = sequence_;
        sequence_ = 0;
    }
    else
        frame.details.sequence = sequence_++;

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

void Protocol::send_handshake_or_goodbye(size_t timeout, bool is_handshake)
{
    auto timer_start(sprot::implementation::check_time_out(timeout));

    if (is_handshake)
    {
        stored_writes_.clear();
        make_frame(Frame_Type::Handshake_Frame);
    }
    else
        make_frame(Frame_Type::Goodbye_Frame);

    send_frame(timeout / 3);
    sprot::implementation::check_time_out(timeout, timer_start);
    receive_ack(timeout / 3);
    sprot::implementation::check_time_out(timeout, timer_start);
    send_ack(timeout / 3);
}

void Protocol::receive_handshake_or_goodbye(size_t timeout, bool is_handshake, bool parital)
{
    auto timer_start(sprot::implementation::check_time_out(timeout));

    if (!parital)
    {
        receive_frame(timeout);
        sprot::implementation::check_time_out(timeout, timer_start);
    }

    Frame_Type read(frame_type(read_buffer_));

    if (is_handshake)
    {
        if (read != Frame_Type::Handshake_Frame)
            THROW2(exceptions::Unexpected_Frame, Frame_Type::Handshake_Frame, read);
    }
    else
    {
        if (read != Frame_Type::Goodbye_Frame)
            THROW2(exceptions::Unexpected_Frame, Frame_Type::Goodbye_Frame, read);
    }

    send_ack(timeout);
    sprot::implementation::check_time_out(timeout, timer_start);
    receive_ack(timeout);
}

void Protocol::receive_anything(size_t timeout)
{
    auto timer_start(sprot::implementation::check_time_out(timeout));

    bool reading = true;
    bool retransmit = false;
    bool failed_retransmit = false;

    unsigned int sequence = 0;

    while (reading)
    {
        auto error_condition = [&]()
        {
            Frame frame(frame_from_buffer(read_buffer_));
            sequence = sequence_;
            if (frame.details.type == Frame_Type::Ack_Frame)
                retransmit = true;
        };

        try
        {
            sprot::implementation::check_time_out(timeout, timer_start);

            if (failed_retransmit && retransmit)
            {
                retransmit = false;
                send_handshake_or_goodbye(timeout);
            }

            if (retransmit)
            {
                make_frame(Frame_Type::Retransmit_Frame);
                send_frame(timeout);
                sequence_ = sequence;
                retransmit = false;
                failed_retransmit = true;
            }

            receive_frame(timeout);

            failed_retransmit = false;

            Frame frame(frame_from_buffer(read_buffer_));
            if (frame.details.type == Frame_Type::Data_Frame)
                reading = false;

            if (frame.details.type == Frame_Type::Ack_Frame)
                send_ack(timeout);

            if (frame.details.type == Frame_Type::Handshake_Frame)
                receive_handshake_or_goodbye(timeout, true, true);

            if (frame.details.type == Frame_Type::Goodbye_Frame)
            {
                receive_handshake_or_goodbye(timeout, false, true);
                memcpy(read_buffer_, frame.bytes, sizeof(frame.bytes));
                reading = false;
            }
        }
        catch (exceptions::Wrong_Number&)
        {
            error_condition();
        }
        catch (exceptions::Crc_Check_Failed&)
        {
            error_condition();
        }
    }
}

void Protocol::send_ack(size_t timeout)
{
    make_frame(Frame_Type::Ack_Frame);
    send_frame(timeout);
}

void Protocol::receive_ack(size_t timeout)
{
    receive_frame(timeout);

    Frame_Type read(frame_type(read_buffer_));
    if (read != Frame_Type::Ack_Frame)
        THROW2(exceptions::Unexpected_Frame, Frame_Type::Ack_Frame, read);
}

bool Protocol::connect(const Params& local_config, Address remote, size_t timeout)
{
    std::lock_guard lock(mutex_);

    if (connected_)
        send_handshake_or_goodbye(op_timeout_, false);

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

    connected_ = false;

    send_handshake_or_goodbye(timeout);

    connected_ = true;

    return connected_;
}

bool Protocol::accept(const Params& local_config, Address remote, size_t timeout)
{
    std::lock_guard lock(mutex_);

    if (connected_)
        send_handshake_or_goodbye(op_timeout_, false);

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

    connected_ = false;

    receive_handshake_or_goodbye(timeout);

    connected_ = true;

    return connected_;
}

void Protocol::send_data_queue(unsigned int starting_sequence, size_t timeout)
{
    if (!l1_transport_)
        THROW(fplog::exceptions::Transport_Missing);

    if (stored_writes_.size() == 0)
        return;

    size_t per_write_to = timeout / stored_writes_.size();

    auto it(stored_writes_.find(starting_sequence));
    for (; it != stored_writes_.end(); ++it)
    {
        Frame frame;
        memcpy(frame.bytes, it->second.buffer, sizeof(frame.bytes));

        size_t written = l1_transport_->write(it->second.buffer, sizeof(frame.bytes) + frame.details.data_len, remote_, per_write_to);
        if (written != (sizeof(frame.bytes) + frame.details.data_len))
            THROW2(exceptions::Size_Mismatch, (sizeof(frame.bytes) + frame.details.data_len), written);

        frame.details.sequence++;
        sequence_ = frame.details.sequence;

        if (frame.details.sequence % no_ack_count_ == 0)
        {
            send_ack(op_timeout_);
            receive_ack(op_timeout_);
        }
    }
}

size_t Protocol::read(void* buf, size_t buf_size, size_t timeout)
{
    std::lock_guard lock(mutex_);

    if (!buf || (buf_size < Max_Frame_Size))
        THROW(fplog::exceptions::Buffer_Overflow);

    receive_anything(timeout);
    Frame frame(frame_from_buffer(read_buffer_));

    memcpy(buf, read_buffer_ + sizeof(frame.bytes), frame.details.data_len);

    return frame.details.data_len;
}

size_t Protocol::write(const void* buf, size_t buf_size, size_t timeout)
{
    std::lock_guard lock(mutex_);

    trim_storage();

    if (sequence_ % no_ack_count_ == 0)
    {
        send_ack(op_timeout_);
        bool doing_retransmit(false);

        try
        {
            receive_ack(op_timeout_);
        }
        catch (exceptions::Unexpected_Frame& e)
        {
            Frame frame(frame_from_buffer(read_buffer_));

            if (frame.details.type != Frame_Type::Retransmit_Frame)
                throw e;

            doing_retransmit = true;
            sequence_ = frame.details.sequence;
        }

        if (doing_retransmit)
        {
            send_data_queue(sequence_, timeout);
        }
    }

    Frame frame(make_frame(Frame_Type::Data_Frame, buf_size, buf));

    Packed_Buffer packet;
    memcpy(packet.buffer, write_buffer_, Max_Frame_Size);

    stored_writes_[frame.details.sequence] = packet;

    send_data_queue(frame.details.sequence, timeout);

    return buf_size;
}


}}
