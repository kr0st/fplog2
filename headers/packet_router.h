#ifndef L1_TRANSPORT_H
#define PACKET_ROUTER_H

#include <sprot.h>
#include <map>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <utils.h>
#include <thread>

namespace sprot
{

class Packet_Router: public Extended_Transport_Interface
{
    public:

        Packet_Router(Extended_Transport_Interface* l0_transport);

        virtual size_t read(void* buf, size_t buf_size, Address& user_data, size_t timeout = infinite_wait);
        virtual size_t write(const void* buf, size_t buf_size, Address& user_data, size_t timeout = infinite_wait);

        ~Packet_Router();


     private:

        Extended_Transport_Interface* l0_transport_;

        struct Read_Request
        {
            unsigned char read_buffer[implementation::Max_Frame_Size];
            size_t read_bytes = 0;
            volatile bool done = false;
            Address read_ext_data;
        };

        std::map<Address, std::vector<Read_Request*>> waitlist_;
        std::mutex waitlist_mutex_;

        Read_Request schedule_read(Address& user_data, size_t timeout = infinite_wait);

        static void reader_thread(Packet_Router* p);
        bool stop_reading_;
        std::thread reader_;

        void waitlist_trace();
};

};

#endif // PACKET_ROUTER_H
