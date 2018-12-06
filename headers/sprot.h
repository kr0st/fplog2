#ifndef SPROT_H
#define SPROT_H

#include <string>
#include <map>
#include <vector>
#include <any>

#include <fplog_exceptions.h>
#include <utils.h>

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

class SPROT_API Protocol_Interface: Basic_Transport_Interface
{
    public:

        typedef std::pair<std::string, std::string> Param;
        typedef std::map<std::string, std::string> Params;

        static Params empty_params;

        virtual bool connect(Protocol_Interface::Params remote, size_t timeout = infinite_wait) = 0;
        virtual bool accept(Protocol_Interface::Params remote, size_t timeout = infinite_wait) = 0;
};

class SPROT_API Extended_Transport_Interface
{
    public:

        struct Address
        {
            unsigned int ip;
            unsigned short port;

            Address(): ip(0), port(0) {}

            bool operator== (const Address& rhs) const
            {
                return (ip == rhs.ip) && (port == rhs.port);
            }

            bool operator< (const Address& rhs) const
            {
                if (ip == rhs.ip)
                    return (port < rhs.port);
                return (ip < rhs.ip);
            }

            Address& from_params(const Protocol_Interface::Params& params)
            {
                ip = 0;
                port = 0;

                if (params.size() < 2)
                    return *this;

                for (auto& param : params)
                {
                    if (generic_util::find_str_no_case(param.first, "ip"))
                    {
                        std::string ip_addr(param.second);
                        ip_addr = generic_util::trim(ip_addr);

                        auto tokens = generic_util::tokenize(ip_addr.c_str(), '.');

                        for (auto& token : tokens)
                        {
                            unsigned short byte = static_cast<unsigned short>(std::stoi(token));
                            ip += byte;
                            ip <<= 8;
                        }
                    }

                    if (generic_util::find_str_no_case(param.first, "port"))
                        port = static_cast<unsigned short>(std::stoi(param.second));
                }

                return *this;
            }
        };

        static Address no_address;
        static const size_t infinite_wait = 4294967295;

        virtual size_t read(void* buf, size_t buf_size, Address& user_data = no_address, size_t timeout = infinite_wait) = 0;
        virtual size_t write(const void* buf, size_t buf_size, Address& user_data = no_address, size_t timeout = infinite_wait) = 0;

        virtual ~Extended_Transport_Interface() {}

        static bool null_data(const Address& user_data) { return (&user_data == &no_address); }
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

        virtual Session* connect(const Protocol_Interface::Params& local_config, const Protocol_Interface::Params& remote, size_t timeout = Session::infinite_wait) = 0;
        virtual Session* accept(const Protocol_Interface::Params& local_config, const Protocol_Interface::Params& remote, size_t timeout = Session::infinite_wait) = 0;

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

    enum Frame_Type
    {
        Handshake_Frame = 0x13,
        Goodbye_Frame,
        Ack_Frame,
        Data_Frame,
        Retransmit_Frame
    };

    const unsigned int Max_Frame_Size = 255;
    const unsigned int Mtu = Max_Frame_Size - sizeof(Frame);

    inline bool crc_check(void* buffer, size_t sz)
    {
        if (sz < sizeof(Frame))
            return false;

        Frame frame;
        memcpy(frame.bytes, buffer, sizeof(Frame));

        if (frame.details.crc != 666)
            return false;

        return true;
    }

}};

#endif // SPROT_H
