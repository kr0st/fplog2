#ifndef SPROT_H
#define SPROT_H

#include <string>
#include <map>
#include <vector>
#include <any>
#include "fplog_exceptions.h"

#ifdef _WIN32

#ifdef SPROT_EXPORT
#define SPROT_API __declspec(dllexport)
#else
#define SPROT_API __declspec(dllimport)
#endif

#endif

#ifdef __linux__
#define SPROT_API
#endif

#ifdef __APPLE__
#define SPROT_API
#endif

namespace sprot {

class SPROT_API Basic_Transport_Interface
{
    public:

        static const size_t infinite_wait = 4294967295;

        virtual size_t read(void* buf, size_t buf_size, size_t timeout = infinite_wait) = 0;
        virtual size_t write(const void* buf, size_t buf_size, size_t timeout = infinite_wait) = 0;

        virtual ~Basic_Transport_Interface() {}
};

class SPROT_API Extended_Transport_Interface
{
    public:

        typedef std::vector<std::any> Extended_Data;

        struct Ip_Port
        {
            unsigned int ip;
            unsigned short port;

            Ip_Port(): ip(0), port(0) {}
            Ip_Port(Extended_Data& ext_data){ from_ext_data(ext_data); }

            bool operator< (const Ip_Port& rhs) const
            {
                if (ip == rhs.ip)
                    return (port < rhs.port);
                return (ip < rhs.ip);
            }

            Ip_Port& from_ext_data(Extended_Data& ext_data)
            {
                if (ext_data.size() < 2)
                {
                    ip = 0;
                    port = 0;

                    return *this;
                }

                try
                {
                    ip = std::any_cast<unsigned int>(ext_data[0]);
                    port = std::any_cast<unsigned short>(ext_data[1]);
                }
                catch (std::bad_cast&)
                {
                    THROWM(fplog::exceptions::Incorrect_Parameter, "Provided extended data contains unexpected data.");
                }

                return *this;
            }
        };

        static Extended_Data no_extended_data;
        static const size_t infinite_wait = 4294967295;

        virtual size_t read(void* buf, size_t buf_size, Extended_Data& user_data = no_extended_data, size_t timeout = infinite_wait) = 0;
        virtual size_t write(const void* buf, size_t buf_size, Extended_Data& user_data = no_extended_data, size_t timeout = infinite_wait) = 0;

        virtual ~Extended_Transport_Interface() {}

        static bool null_data(const Extended_Data& user_data) { return (&user_data == &no_extended_data); }
};


class SPROT_API Session: public Basic_Transport_Interface
{
    public:

        virtual void disconnect();
        virtual ~Session();
};

class SPROT_API Session_Manager
{
    public:

        typedef std::pair<std::string, std::string> Param;
        typedef std::map<std::string, std::string> Params;

        static Params empty_params;

        virtual Session* connect(const Params& from, const Params& to) = 0;
        virtual Session* accept(const Params& config, size_t timeout = Session::infinite_wait) = 0;

        virtual ~Session_Manager();
};

namespace implementation
{
    union Frame
    {
        struct
        {
            unsigned short crc;
            unsigned short type;
            unsigned int origin_ip;
            unsigned short origin_listen_port;
            char hostname[16];
            unsigned int sequence;
            unsigned short data_len;
        } details;

        unsigned char bytes[32];
    };

    const unsigned int Max_Frame_Size = 255;
    const unsigned int Mtu = Max_Frame_Size - sizeof (Frame);
};

};

#endif // SPROT_H
