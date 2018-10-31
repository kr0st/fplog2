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

class Udp_Transport: public sprot::Basic_Transport_Interface
{
    public:

        void connect(const Session_Manager::Params& params);
        void disconnect();

        virtual size_t read(void* buf, size_t buf_size, size_t timeout = infinite_wait);
        virtual size_t write(const void* buf, size_t buf_size, size_t timeout = infinite_wait);

        Udp_Transport();
        virtual ~Udp_Transport();


    private:

        SOCKET socket_;
        bool connected_;
        std::recursive_mutex mutex_;
        unsigned char ip_[4];
        unsigned short port_;
        bool localhost_;
};

};
