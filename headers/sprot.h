#ifndef SPROT_H
#define SPROT_H

#include <string>
#include <map>
#include <vector>
#include <chrono>

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
#include <netinet/in.h>
#define SPROT_API
#endif

#ifdef __APPLE__
#define SPROT_API
#endif

namespace sprot {

typedef std::pair<std::string, std::string> Param;
typedef std::map<std::string, std::string> Params;

static Params empty_params;

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

    Address& from_params(const Params& params)
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

                int c = 0;
                for (auto& token : tokens)
                {
                    unsigned short byte = static_cast<unsigned short>(std::stoi(token));
                    ip += byte;
                    c++;
                    if (c < 4)
                        ip <<= 8;
                }

                ip = htonl(ip);
            }

            if (generic_util::find_str_no_case(param.first, "port"))
                port = static_cast<unsigned short>(std::stoi(param.second));
        }

        return *this;
    }
};

static Address no_address;

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

        static const size_t infinite_wait = 4294967295;

        virtual size_t read(void* buf, size_t buf_size, Address& user_data = no_address, size_t timeout = infinite_wait) = 0;
        virtual size_t write(const void* buf, size_t buf_size, Address& user_data = no_address, size_t timeout = infinite_wait) = 0;

        virtual ~Extended_Transport_Interface() {}

        static bool null_data(const Address& user_data) { return (&user_data == &no_address); }
};

class SPROT_API Protocol_Interface: public Basic_Transport_Interface
{
    public:

        virtual bool connect(const Params& local_config, Address remote, size_t timeout = infinite_wait) = 0;
        virtual bool accept(const Params& local_config, Address remote, size_t timeout = infinite_wait) = 0;
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
        virtual Session* connect(const Params& local_config, const Params& remote, size_t timeout = Session::infinite_wait) = 0;
        virtual Session* accept(const Params& local_config, const Params& remote, size_t timeout = Session::infinite_wait) = 0;

        virtual ~Session_Manager();
};

namespace exceptions
{
    class Unknown_Frame : public fplog::exceptions::Generic_Exception
    {
        public:

            Unknown_Frame(const char* facility, const char* file, int line, int frame_type):
                Generic_Exception(facility, file, line)
            {
                char str[40];
                sprintf(str, "Unknown frame type %d detected.", frame_type);
                message_ = str;
            }
    };

    class Unexpected_Frame : public fplog::exceptions::Generic_Exception
    {
        public:

            Unexpected_Frame(const char* facility, const char* file, int line, int expected, int actual):
                Generic_Exception(facility, file, line)
            {
                char str[255];
                sprintf(str, "Unexpected frame type %d detected when %d was expected.", actual, expected);
                message_ = str;
            }
    };

    class Size_Mismatch : public fplog::exceptions::Generic_Exception
    {
        public:

            Size_Mismatch(const char* facility, const char* file, int line, size_t expected, size_t actual):
                Generic_Exception(facility, file, line)
            {
                char str[255];
                sprintf(str, "Sent or received bytes mismatch: expected %lu, got %lu.", expected, actual);
                message_ = str;
            }
    };

    class Crc_Check_Failed : public fplog::exceptions::Generic_Exception
    {
        public:

            Crc_Check_Failed(const char* facility, const char* file, int line, int expected, int actual):
                Generic_Exception(facility, file, line)
            {
                char str[255];
                sprintf(str, "CRC check failed: expected crc = %d, got crc = %d.", expected, actual);
                message_ = str;
            }
    };

    class Wrong_Number : public fplog::exceptions::Generic_Exception
    {
        public:

            Wrong_Number(const char* facility, const char* file, int line, unsigned int expected, unsigned int actual):
                Generic_Exception(facility, file, line)
            {
                char str[255];
                sprintf(str, "Sequence number check failed: expected %d, got %d.", expected, actual);
                message_ = str;
            }
    };

    class Connection_Broken : public fplog::exceptions::Generic_Exception
    {
        public:

            Connection_Broken(const char* facility, const char* file, int line):
                Generic_Exception(facility, file, line)
            {
                char str[255];
                sprintf(str, "Connection broke down, please redo accept/connect.");
                message_ = str;
            }
    };

    class Repeat_Retransmit : public fplog::exceptions::Generic_Exception
    {
        public:

            Repeat_Retransmit(const char* facility, const char* file, int line):
                Generic_Exception(facility, file, line)
            {
            }
    };
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
            char hostname[18];
            unsigned int sequence;
            unsigned short data_len;
        } details;

        unsigned char bytes[36];
    };

    enum Frame_Type
    {
        Handshake_Frame = 0x13,
        Goodbye_Frame,
        Ack_Frame,
        Nack_Frame,
        Data_Frame,
        Retransmit_Frame,
        Unknown_Frame
    };

    const unsigned int Max_Frame_Size = 255;
    const unsigned int Mtu = Max_Frame_Size - sizeof(Frame::bytes);

    inline bool crc_check(void* buffer, size_t sz, unsigned short* expected = nullptr, unsigned short* actual = nullptr)
    {
        if (sz < sizeof(Frame::bytes))
            return false;

        if (sz > Max_Frame_Size)
            return false;

        Frame frame;
        memcpy(frame.bytes, buffer, sizeof(frame.bytes));

        unsigned short crc_expected = generic_util::gen_simple_crc16(static_cast<unsigned char*>(buffer) + 2, static_cast<unsigned short>(sz) - 2);
        unsigned short crc_actual = frame.details.crc;

        if (expected)
            *expected = crc_expected;
        if (actual)
            *actual = crc_actual;

        if (crc_actual != crc_expected)
            return false;

        return true;
    }

    inline auto check_time_out(unsigned long long timeout,
                               std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration> timer_start = std::chrono::system_clock::now())
    {
        auto timer_start_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(timer_start);
        auto timer_stop_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        std::chrono::milliseconds timeout_ms(timeout);

        if (timer_stop_ms - timer_start_ms >= timeout_ms)
            THROW(fplog::exceptions::Timeout);

        return timer_start;
    };

}};

#endif // SPROT_H
