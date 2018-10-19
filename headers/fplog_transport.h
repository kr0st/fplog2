#pragma once

#include <map>
#include <string>

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


namespace fplog {

class SPROT_API Transport_Interface
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

};
