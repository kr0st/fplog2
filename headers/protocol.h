#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <mutex>
#include <queue>

#include <sprot.h>
#include <stdint.h>

namespace sprot { namespace implementation {

class Protocol: public Protocol_Interface
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
            connected_ = false;
        }


    private:

        bool connected_ = false, acceptor_ = false;
        Address local_, remote_;

        unsigned int send_sequence_ = 0;
        unsigned int recv_sequence_ = 0;

        ///options
        const unsigned int no_ack_count_ = 5;
        const unsigned int storage_max_ = 100;
        const unsigned int storage_trim_ = 50;
        const unsigned int op_timeout_ = 500;
        const unsigned int max_retries_ = 5;
        ///options

        std::recursive_mutex mutex_;

        Extended_Transport_Interface* l1_transport_ = nullptr;

        unsigned char read_buffer_[Max_Frame_Size];
        unsigned char write_buffer_[Max_Frame_Size];

        struct Packed_Buffer { unsigned char buffer[Max_Frame_Size]; };
        std::map<unsigned int, Packed_Buffer> stored_writes_;
        std::map<unsigned int, Packed_Buffer> stored_reads_;
        std::queue<unsigned int> recovered_frames_;

        char localhost_[18];

        void trim_storage(std::map<unsigned int, Packed_Buffer>& storage);
        void empty_storage(std::map<unsigned int, Packed_Buffer>& storage);
        void put_in_storage(std::map<unsigned int, Packed_Buffer>& storage, unsigned int sequence, const void* buf);
        void take_from_storage(std::map<unsigned int, Packed_Buffer>& storage, unsigned int sequence, void* buf);

        Frame make_frame(Frame_Type type, size_t data_len = 0, const void* data = nullptr);

        void send_frame(size_t timeout);
        Frame receive_frame(size_t timeout);

        bool retransmit_request(std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration> timer_start,
                                size_t timeout, unsigned int last_received_sequence);
        bool retransmit_response(std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration> timer_start, size_t timeout);
};

}}

#endif // PROTOCOL_H
