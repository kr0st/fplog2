#include <mutex>
#include <sprot.h>

namespace sprot { namespace implementation {

class Protocol: Protocol_Interface
{
    public:

        size_t read(void* buf, size_t buf_size, size_t timeout = infinite_wait);
        size_t write(const void* buf, size_t buf_size, size_t timeout = infinite_wait);

        virtual bool connect(const Params& local_config, Address remote, size_t timeout = infinite_wait) = 0;
        virtual bool accept(const Params& local_config, Address remote, size_t timeout = infinite_wait) = 0;

        Protocol(Extended_Transport_Interface* l1_transport): l1_transport_(l1_transport){}

        virtual ~Protocol()
        {
            std::lock_guard lock(mutex_);
            send_goodbye(op_timeout_);
            connected_ = false;
        }


    private:

        bool connected_;
        Address local_, remote_;

        unsigned int sequence_;
        unsigned int op_timeout_;

        std::recursive_mutex mutex_;

        Extended_Transport_Interface* l1_transport_;

        unsigned char read_buffer_[Max_Frame_Size];
        unsigned char write_buffer_[Max_Frame_Size];

        char localhost_[18];

        void send_frame(size_t timeout);
        void receive_frame(size_t timeout);

        Frame_Type frame_type(void* buffer);
        void make_frame(Frame_Type type, size_t data_len = 0, void* data = nullptr);

        void send_handshake(size_t timeout);
        void receive_handshake(size_t timeout);

        void send_goodbye(size_t timeout);
        void receive_goodbye(size_t timeout);

        void send_ack(size_t timeout);
        void receive_ack(size_t timeout);
};

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
}

Frame_Type frame_type(void* buffer)
{
    if (buffer)
    {
        Frame frame;
        memcpy(frame.bytes, buffer, sizeof(frame.bytes));
        if (frame.details.type < Frame_Type::Unknown_Frame)
            return Frame_Type(frame.details.type);
    }

    return Frame_Type::Unknown_Frame;
}

void Protocol::make_frame(Frame_Type type, size_t data_len, void* data)
{
    if (type > Frame_Type::Unknown_Frame)
        THROW1(exceptions::Unknown_Frame, type);

    Frame frame;

    frame.details.type = type;
    frame.details.data_len = static_cast<unsigned short>(data_len);
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
}

void Protocol::send_handshake(size_t timeout)
{
}

void Protocol::receive_handshake(size_t timeout)
{
}

void Protocol::send_goodbye(size_t timeout)
{
}

void Protocol::receive_goodbye(size_t timeout)
{
}

void Protocol::send_ack(size_t timeout)
{
}

void Protocol::receive_ack(size_t timeout)
{
}

bool Protocol::connect(const Params& local_config, Address remote, size_t timeout)
{
    std::lock_guard lock(mutex_);

    if (connected_)
        send_goodbye(op_timeout_);

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

    receive_handshake(timeout);

    connected_ = true;

    return connected_;
}


}}
