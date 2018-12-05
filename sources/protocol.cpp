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
        virtual ~Protocol();


    private:

        bool connected_;
        Extended_Transport_Interface::Ip_Port endpoint_;

        unsigned int sequence_;
        unsigned int op_timeout_;

        std::recursive_mutex mutex_;


        unsigned char read_buffer_[Max_Frame_Size];
        unsigned char write_buffer_[Max_Frame_Size];

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

Protocol::~Protocol()
{
    std::lock_guard lock(mutex_);
    send_goodbye(op_timeout_);
    connected_ = false;
}

bool Protocol::connect(Protocol_Interface::Params params, size_t timeout)
{
    std::lock_guard lock(mutex_);

    if (connected_)
        send_goodbye(op_timeout_);

    endpoint_.from_params(params);
    connected_ = false;

    receive_handshake(timeout);

    connected_ = true;

    return connected_;
}


}}
