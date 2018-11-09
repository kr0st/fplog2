#ifndef L1_TRANSPORT_H
#define L1_TRANSPORT_H

#include <sprot.h>

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

        size_t internal_read(void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout = infinite_wait);
};

};

#endif // L1_TRANSPORT_H
