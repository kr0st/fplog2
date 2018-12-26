#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <mutex>
#include <sprot.h>
#include <stdint.h>

namespace sprot { namespace implementation {

class Protocol: Protocol_Interface
{
    public:

        size_t read(void* buf, size_t buf_size, size_t timeout = infinite_wait);
        size_t write(const void* buf, size_t buf_size, size_t timeout = infinite_wait);

        virtual bool connect(const Params& local_config, Address remote, size_t timeout = infinite_wait);
        virtual bool accept(const Params& local_config, Address remote, size_t timeout = infinite_wait);

        Protocol(Extended_Transport_Interface* l1_transport): l1_transport_(l1_transport){}

        virtual ~Protocol()
        {
            std::lock_guard lock(mutex_);

            try
            {
                send_handshake_or_goodbye(op_timeout_, false);
            }
            catch(...)
            {
            }

            connected_ = false;
        }


    private:

        bool connected_ = false, acceptor_;
        Address local_, remote_;

        unsigned int sequence_;
        unsigned int fail_counter_;

        ///options
        const unsigned int no_ack_count_ = 5;
        const unsigned int storage_max_ = 100;
        const unsigned int storage_trim_ = 50;
        const unsigned int op_timeout_ = 500;
        ///options

        std::recursive_mutex mutex_;

        Extended_Transport_Interface* l1_transport_;

        unsigned char read_buffer_[Max_Frame_Size];
        unsigned char write_buffer_[Max_Frame_Size];

        struct Packed_Buffer { unsigned char buffer[Max_Frame_Size]; };
        std::map<unsigned int, Packed_Buffer> stored_writes_;

        char localhost_[18];

        void trim_storage();

        void send_frame(size_t timeout);
        void receive_frame(size_t timeout);

        Frame make_frame(Frame_Type type, size_t data_len = 0, const void* data = nullptr);

        void send_handshake_or_goodbye(size_t timeout, bool is_handshake = true);
        void receive_handshake_or_goodbye(size_t timeout, bool is_handshake = true, bool parital = false);

        void send_data_queue(unsigned int starting_sequence, size_t timeout);

        void receive_anything(size_t timeout);

        void send_ack(size_t timeout);
        void receive_ack(size_t timeout);
};

}}

#endif // PROTOCOL_H