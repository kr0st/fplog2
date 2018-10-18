#pragma once

#include <map>
#include <string>

#include "fplog_exceptions.h"

#ifdef SPROT_EXPORT
#define SPROT_API __declspec(dllexport)
#else
#define SPROT_API __declspec(dllimport)
#endif

#ifdef _LINUX
#define SPROT_API 
#endif

namespace fplog {

class Transport_Interface
{
    public:

        typedef std::pair<std::string, std::string> Param;
        typedef std::map<std::string, std::string> Params;
        
        static Params empty_params;
        static const size_t infinite_wait = 4294967295;

        virtual void connect(const Params& params) {}
        virtual void disconnect() {}

        virtual size_t read(void* buf, size_t buf_size, size_t timeout = infinite_wait) = 0;
        virtual size_t write(const void* buf, size_t buf_size, size_t timeout = infinite_wait) = 0;

        virtual ~Transport_Interface(){};
};

class Transport_Factory
{
    public:

        virtual Transport_Interface* create(const Transport_Interface::Params& params) = 0;
};

struct SPROT_API UID
{
    UID(): high(0), low(0) {}
    bool operator== (const UID& rhs) const { return ((high == rhs.high) && (low == rhs.low)); }
    bool operator< (const UID& rhs) const
    {
        if (*this == rhs)
            return false;

        if (high < rhs.high)
            return true;

        if (high == rhs.high)
            return (low < rhs.low);

        return false;
    }

    std::string to_string(UID& uid)
    {
        return (std::to_string(uid.high) + "_" + std::to_string(uid.low));
    }

    UID from_string(const std::string& str);

    unsigned long long high;
    unsigned long long low;
    
    struct SPROT_API Helper
    {
        static UID from_string(const std::string& str);
    };
};

};
