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
        virtual bool accept(const Params& local_config, Address& remote, size_t timeout = infinite_wait);

        Protocol(Extended_Transport_Interface* l1_transport): l1_transport_(l1_transport)
        {
            read_buffer_ = new unsigned char[options.max_frame_size];
            write_buffer_ = new unsigned char[options.max_frame_size];
        }

        virtual ~Protocol()
        {
            std::lock_guard lock(mutex_);
            connected_ = false;

            delete [] read_buffer_;
            delete [] write_buffer_;
        }


    private:

        Protocol();

        bool connected_ = false, acceptor_ = false;
        Address local_, remote_, accepted_remote_;

        unsigned int send_sequence_ = 0;
        unsigned int recv_sequence_ = 0;
        unsigned int prev_seq_ = UINT32_MAX - 2;

        std::recursive_mutex mutex_;

        Extended_Transport_Interface* l1_transport_ = nullptr;

        unsigned char* read_buffer_;
        unsigned char* write_buffer_;

        struct Packed_Buffer
        {
            unsigned char* buffer = nullptr;

            Packed_Buffer(){ buffer = new unsigned char[options.max_frame_size]; }
            ~Packed_Buffer() { delete [] buffer; }
            Packed_Buffer(const Packed_Buffer& rhs): Packed_Buffer() { operator=(rhs); }
            Packed_Buffer& operator=(const Packed_Buffer& rhs)
            {
                if ((buffer != nullptr) && (rhs.buffer != nullptr)) memcpy(buffer, rhs.buffer, options.max_frame_size);
                return *this;
            }
        };

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
