#pragma once

#include <fplog_exceptions.h>
#include <sprot.h>

#ifndef _WIN32

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#define SOCKET int
#define SD_BOTH SHUT_RDWR
#define closesocket close
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1

#else

#include <winsock2.h>

#endif

#include <mutex>

namespace sprot {

class Udp_Transport: public sprot::Extended_Transport_Interface
{
    public:

        union Ip_Address
        {
            unsigned char bytes[4];
            unsigned int ip;
        };

        void enable(const Protocol_Interface::Params& params);
        void disable();

        virtual size_t read(void* buf, size_t buf_size, Extended_Data& user_data = no_extended_data, size_t timeout = infinite_wait);
        virtual size_t write(const void* buf, size_t buf_size, Extended_Data& user_data, size_t timeout = infinite_wait);

        Udp_Transport();
        virtual ~Udp_Transport();


    private:

        SOCKET socket_;
        bool enabled_;

        std::recursive_mutex read_mutex_;
        std::recursive_mutex write_mutex_;

        unsigned char ip_[4];
        unsigned short port_;

        bool localhost_;
};

};
