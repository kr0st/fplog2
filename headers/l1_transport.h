#ifndef L1_TRANSPORT_H
#define L1_TRANSPORT_H

#include <sprot.h>
#include <map>
#include <condition_variable>
#include <mutex>

namespace sprot
{

class L1_transport: public Extended_Transport_Interface
{
    public:

        L1_transport(Extended_Transport_Interface* l0_transport);

        virtual size_t read(void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout = infinite_wait);
        virtual size_t write(const void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout = infinite_wait);

        ~L1_transport();


     private:

        Extended_Transport_Interface* l0_transport_;
        unsigned char read_buffer_[implementation::Max_Frame_Size];
        std::mutex read_buffer_mutex_;
        size_t read_bytes_;

        std::map<Extended_Data, std::condition_variable*> waitlist_;
        std::mutex waitlist_mutex_;

        std::condition_variable read_signal_;
        std::mutex read_signal_mutex_;

        size_t schedule_read(Extended_Data& user_data, size_t timeout = infinite_wait);
        size_t internal_read(void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout = infinite_wait);
};

};

#endif // L1_TRANSPORT_H
