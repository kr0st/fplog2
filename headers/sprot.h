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

        static Extended_Data no_extended_data;
        static const size_t infinite_wait = 4294967295;

        virtual size_t read(void* buf, size_t buf_size, size_t timeout = infinite_wait, Extended_Data& user_data = no_extended_data) = 0;
        virtual size_t write(const void* buf, size_t buf_size, size_t timeout = infinite_wait, Extended_Data& user_data = no_extended_data) = 0;

        virtual ~Extended_Transport_Interface() {}


    protected:

        bool null_data(const Extended_Data& user_data) { return (&user_data == &no_extended_data); }
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

        virtual Session* create(const Params& params) = 0;

        virtual ~Session_Manager();
};

};

#endif // SPROT_H
