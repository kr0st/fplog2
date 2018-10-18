#pragma once

#include <string>
#include <stdlib.h>
#include <stdio.h>

#ifndef __SHORT_FORM_OF_FILE__

#ifdef WIN32
#define __SHORT_FORM_OF_FILE_WIN__ \
    (strrchr(__FILE__,'\\') \
    ? strrchr(__FILE__,'\\')+1 \
    : __FILE__ \
    )
#define __SHORT_FORM_OF_FILE__ __SHORT_FORM_OF_FILE_WIN__
#else
#define __SHORT_FORM_OF_FILE_NIX__ \
    (strrchr(__FILE__,'/') \
    ? strrchr(__FILE__,'/')+1 \
    : __FILE__ \
    )
#define __SHORT_FORM_OF_FILE__ __SHORT_FORM_OF_FILE_NIX__
#endif

#endif

#define THROW(exception_type) { throw exception_type(__FUNCTION__, __SHORT_FORM_OF_FILE__, __LINE__); }
#define THROWM(exception_type, message) { throw exception_type(__FUNCTION__, __SHORT_FORM_OF_FILE__, __LINE__, message); }

#ifdef _LINUX
#define NO_ITOA
#endif

#ifdef __APPLE__
#define NO_ITOA
#endif

#ifdef NO_ITOA
inline char *itoa(long i, char* s, int dummy_radix) {
    sprintf(s, "%ld", i);
    return s;
}
#endif

namespace fplog { namespace exceptions {

class Generic_Exception
{
    public:

        Generic_Exception(const char* facility, const char* file = "", int line = 0, const char* message = ""):
        facility_(facility),
        message_(message),
        file_(file),
        line_(line)
        {
        }

        std::string what()
        {
            char buf[256];
			
			#ifdef _WIN32
				_itoa_s(line_, buf, sizeof(buf) / sizeof(char), 10);
			#else
				itoa(line_, buf, 10);
			#endif
			
            return "[" + facility_ + ", f:" + file_ + ", l:" + buf + "] " + message_;
        }


    protected:

        std::string facility_;
        std::string message_;
        std::string file_;
        int line_;


    private:

        Generic_Exception();
};

class Connect_Failed: public Generic_Exception
{
    public:

        Connect_Failed(const char* facility, const char* file = "", int line = 0, const char* message = "Cannot establish a connection."):
        Generic_Exception(facility, file, line, message)
        {
        }
};

class Write_Failed: public Generic_Exception
{
    public:

        Write_Failed(const char* facility, const char* file = "", int line = 0, const char* message = "Write operation failed."):
        Generic_Exception(facility, file, line, message)
        {
        }
};

class Read_Failed: public Generic_Exception
{
    public:

        Read_Failed(const char* facility, const char* file = "", int line = 0, const char* message = "Read operation failed."):
        Generic_Exception(facility, file, line, message)
        {
        }
};

class Incorrect_Parameter: public Generic_Exception
{
    public:

        Incorrect_Parameter(const char* facility, const char* file = "", int line = 0, const char* message = "Parameter supplied to function has incorrect value."):
        Generic_Exception(facility, file, line, message)
        {
        }
};

class Not_Implemented: public Generic_Exception
{
    public:

        Not_Implemented(const char* facility, const char* file = "", int line = 0, const char* message = "Feature not implemented yet."):
        Generic_Exception(facility, file, line, message)
        {
        }
};

class Buffer_Overflow: public Generic_Exception
{
    public:

        Buffer_Overflow(const char* facility, const char* file = "", int line = 0, const char* message = "Buffer too small.", size_t buf_sz = 0):
        Generic_Exception(facility, file, line, message),
        buf_sz_(buf_sz)
        {
        }

        size_t get_required_size() { return buf_sz_; }


    private:

        size_t buf_sz_;
};

class Timeout: public Generic_Exception
{
    public:

        Timeout(const char* facility, const char* file = "", int line = 0, const char* message = "Timeout while reading or writing."):
        Generic_Exception(facility, file, line, message)
        {
        }
};

class Transport_Missing : public Generic_Exception
{
public:

    Transport_Missing(const char* facility, const char* file = "", int line = 0, const char* message = "Transport or protocol is missing.") :
        Generic_Exception(facility, file, line, message)
    {
    }
};

class Invalid_Uid: public Generic_Exception
{
    public:

        Invalid_Uid(const char* facility, const char* file = "", int line = 0, const char* message = "Supplied fplog::UID is invalid."):
        Generic_Exception(facility, file, line, message)
        {
        }
};

}};
