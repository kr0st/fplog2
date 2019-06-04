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
            unsigned char* read_buffer = nullptr;

            size_t read_bytes = 0;
            volatile bool done = false;
            Address read_ext_data;

            Read_Request(){ read_buffer = new unsigned char[implementation::options.max_frame_size]; }
            ~Read_Request() { delete [] read_buffer; }
            Read_Request(const Read_Request& rhs): Read_Request() { operator=(rhs); }
            Read_Request& operator=(const Read_Request& rhs)
            {
                if ((read_buffer != nullptr) && (rhs.read_buffer != nullptr))
                    memcpy(read_buffer, rhs.read_buffer, implementation::options.max_frame_size);

                read_bytes = rhs.read_bytes;
                done = rhs.done;
                read_ext_data = rhs.read_ext_data;

                return *this;
            }
        };

        std::map<Address, std::vector<Read_Request*>> waitlist_;
        std::mutex waitlist_mutex_;

        Read_Request schedule_read(Address& user_data, size_t timeout = infinite_wait);


        static void waste_management_thread(Packet_Router* p);
        static void reader_thread(Packet_Router* p);

        bool stop_reading_;

        std::thread reader_;
        std::thread garbage_collector_;

        void waitlist_trace();
};

};

#endif // PACKET_ROUTER_H
