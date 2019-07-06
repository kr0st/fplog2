#pragma once

#include <string>
#include <set>
#include <vector>
#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <typeinfo>
#include <sprot.h>
#include <fplog_exceptions.h>
#include <algorithm>
#include <mutex>
#include <functional>

#ifdef FPLOG_EXPORT

#ifdef _LINUX
#define FPLOG_API __attribute__ ((visibility("default")))
#else
#define FPLOG_API __declspec(dllexport)
#endif

#else

#ifdef _LINUX
#define FPLOG_API 
#endif

#ifdef __APPLE__
#define FPLOG_API
#endif

#ifdef _WIN32_WINNT
#define FPLOG_API __declspec(dllimport)
#endif

#endif

#ifdef _LINUX
namespace fplog { std::string demangle_cpp_name(const char* mangled); };
#define CLASSNAME fplog::demangle_cpp_name(typeid(*this).name()).c_str()
#endif

#ifdef _WIN32_WINNT
#define CLASSNAME typeid(*this).name()
#endif

#define CLASSNAME_SHORT \
    (strrchr(CLASSNAME,' ') \
    ? strrchr(CLASSNAME,' ')+1 \
    : CLASSNAME \
    )

#define FUNCTION_SHORT \
    (strrchr(__FUNCTION__,':') \
    ? strrchr(__FUNCTION__,':')+1 \
    : __FUNCTION__ \
    )
    
#ifndef _WIN32_WINNT

#define FPL_TRACE(format, ...) fplog::Message(fplog::Prio::debug, fplog::get_facility(), format, ##__VA_ARGS__).set_module(__SHORT_FORM_OF_FILE__).set_line(__LINE__).set_method(FUNCTION_SHORT)
#define FPL_INFO(format, ...) fplog::Message(fplog::Prio::info, fplog::get_facility(), format, ##__VA_ARGS__).set_module(__SHORT_FORM_OF_FILE__).set_line(__LINE__).set_method(FUNCTION_SHORT)
#define FPL_WARN(format, ...) fplog::Message(fplog::Prio::warning, fplog::get_facility(), format, ##__VA_ARGS__).set_module(__SHORT_FORM_OF_FILE__).set_line(__LINE__).set_method(FUNCTION_SHORT)
#define FPL_ERROR(format, ...) fplog::Message(fplog::Prio::error, fplog::get_facility(), format, ##__VA_ARGS__).set_module(__SHORT_FORM_OF_FILE__).set_line(__LINE__).set_method(FUNCTION_SHORT)

#define FPL_CTRACE(format, ...) FPL_TRACE(format, ##__VA_ARGS__).set_class(CLASSNAME_SHORT)
#define FPL_CINFO(format, ...) FPL_INFO(format, ##__VA_ARGS__).set_class(CLASSNAME_SHORT)
#define FPL_CWARN(format, ...) FPL_WARN(format, ##__VA_ARGS__).set_class(CLASSNAME_SHORT)
#define FPL_CERROR(format, ...) FPL_ERROR(format, ##__VA_ARGS__).set_class(CLASSNAME_SHORT)

#else

#define FPL_TRACE(format, ...) fplog::Message(fplog::Prio::debug, fplog::get_facility(), format, __VA_ARGS__).set_module(__SHORT_FORM_OF_FILE__).set_line(__LINE__).set_method(FUNCTION_SHORT)
#define FPL_INFO(format, ...) fplog::Message(fplog::Prio::info, fplog::get_facility(), format, __VA_ARGS__).set_module(__SHORT_FORM_OF_FILE__).set_line(__LINE__).set_method(FUNCTION_SHORT)
#define FPL_WARN(format, ...) fplog::Message(fplog::Prio::warning, fplog::get_facility(), format, __VA_ARGS__).set_module(__SHORT_FORM_OF_FILE__).set_line(__LINE__).set_method(FUNCTION_SHORT)
#define FPL_ERROR(format, ...) fplog::Message(fplog::Prio::error, fplog::get_facility(), format, __VA_ARGS__).set_module(__SHORT_FORM_OF_FILE__).set_line(__LINE__).set_method(FUNCTION_SHORT)

#define FPL_CTRACE(format, ...) FPL_TRACE(format, __VA_ARGS__).set_class(CLASSNAME_SHORT)
#define FPL_CINFO(format, ...) FPL_INFO(format, __VA_ARGS__).set_class(CLASSNAME_SHORT)
#define FPL_CWARN(format, ...) FPL_WARN(format, __VA_ARGS__).set_class(CLASSNAME_SHORT)
#define FPL_CERROR(format, ...) FPL_ERROR(format, __VA_ARGS__).set_class(CLASSNAME_SHORT)

#endif

namespace fplogd
{
    class Impl;
};

namespace fplog
{

struct FPLOG_API Facility
{
    static const char* system; //message from some system component
    static const char* user; //message from user-level component
    static const char* security; //security or authorization related message
    static const char* fplog; //message from fplog directly, could inform about log system malfunction
};

struct FPLOG_API Prio
{
    static const char* emergency; //system is unusable
    static const char* alert; //action must be taken immediately
    static const char* critical; //critical conditions
    static const char* error; //error conditions
    static const char* warning; //warning conditions
    static const char* notice; //normal but significant condition
    static const char* info; //informational
    static const char* debug; //debug/trace info for developers
};

class FPLOG_API Message
{
    friend class Fplog_Impl;
    friend class fplogd::Impl;

    public:

        struct FPLOG_API Mandatory_Fields
        {
            static const char* facility; //according to syslog
            static const char* priority; //same as severity for syslog
            static const char* timestamp; //ISO8601 timestamp with milliseconds and timezone
            static const char* hostname; //IP address or any specific sending device id, added by fplogd before sending
            static const char* appname; //name of application or service using this logging library, needed for fplog IPC
        };

        struct FPLOG_API Optional_Fields
        {
            static const char* text; //log message text
            static const char* component; //package name or any logical software component
            static const char* class_name; //class name if OOP is used
            static const char* method; //method of a given class if OOP is used or just a function name
            static const char* module; //source file name
            static const char* line; //line number in the above mentioned source file
            static const char* options; //for example encryption method + options when encryption is in use
            static const char* encrypted; //true/false, if true then Text field contains encrypted JSON values - 
                                          //the rest of the log message including the plaintext version of Text field
            static const char* file; //filename when sending a file inside the log message
            static const char* /*the*/blob; //used when attaching binary fields to the message, resulting JSON object will look
                                            //like this: "blob_name":{ "blob":"xckjhKJSHDKDSdJKShdsdsgr=" }
                                            //where "xckjhKJSHDKDSdJKShdsdsgr=" is base64 encoded binary object
            static const char* warning; //contains warning for the user in case there was an issue with this specific log message
            static const char* sequence; //sequence number that allows to prevent duplicate messages and also to tell
                                         //which message was first even if timestamps are the same
            static const char* batch; //indicator if this message is actually a container for N other shorter messages
        };

        Message(const char* prio, const char *facility, const char* format = 0, ...);
        Message(const rapidjson::Document& msg);
        Message(const std::string& msg);

        Message(const Message &obj)
        {
            validate_params_ = obj.validate_params_;
            msg_.CopyFrom(obj.msg_, msg_.GetAllocator());
        }
        Message& operator= (const Message& rhs)
        {
            validate_params_ = rhs.validate_params_;
            msg_.CopyFrom(rhs.msg_, msg_.GetAllocator());
            return *this;
        }

        Message& set_timestamp(const char* timestamp = 0); //either sets provided timestamp or uses current system date/time if timestamp is 0

        Message& add(const char* param_name, int param){ return add<int>(param_name, param); }
        Message& add(const char* param_name, long long int param){ return add<long long int>(param_name, param); }
        Message& add(const char* param_name, double param){ return add<double>(param_name, param); }
        Message& add(const char* param_name, std::string& param){ return add<std::string>(param_name, param); }
        Message& add(const char* param_name, const char* param){ return add<const char*>(param_name, param); }
        Message& add_binary(const char* param_name, const void* buf, size_t buf_size_bytes);

        //before adding JSON element make sure it has a name
        Message& add(rapidjson::Document& param);
        Message& add(const std::string& json); //here adding json object that is encoded in plaintext string

        Message& add_batch(rapidjson::Document& batch);
        bool has_batch();
        rapidjson::Document get_batch();

        Message& set_text(std::string& text);
        Message& set_text(const char* text);

        Message& set_class(std::string& class_name);
        Message& set_class(const char* class_name);

        Message& set_module(std::string& module);
        Message& set_module(const char* module);

        Message& set_method(std::string& method);
        Message& set_method(const char* method);

        Message& set_line(int line);
        Message& set_file(const char* name);

        std::string as_string() const;
        rapidjson::Document& as_json();


    private:

        Message();

        template <typename T> Message& set(const char* param_name, T param)
        {
            validate_params_ = false;
            
            Message& msg(*this);

            try
            {
                msg = add(param_name, param);
            }
            catch(std::exception& e)
            {
                validate_params_ = true;
                throw e;
            }
            catch(fplog::exceptions::Generic_Exception& e)
            {
                validate_params_ = true;
                throw e;
            }

            validate_params_ = true;
            return msg;
        }
    
        // trim from start
        static inline std::string &ltrim(std::string &s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int c) {return !std::isspace(c);}));
            return s;
        }
        
        // trim from end
        static inline std::string &rtrim(std::string &s) {
            s.erase(std::find_if(s.rbegin(), s.rend(), [](int c) {return !std::isspace(c);}).base(), s.end());
            return s;
        }
        
        // trim from both ends
        static inline std::string &trim(std::string &s) {
            return ltrim(rtrim(s));
        }
    
        template <typename T> bool is_valid(const char* param_name, T param)
        {
            if (!validate_params_)
                return true;

            if (!param_name)
                return false;

            std::string lowercased(param_name);
            trim(lowercased);

            std::transform(lowercased.begin(), lowercased.end(), lowercased.begin(), ::tolower);

            bool valid(std::find(reserved_names_.begin(), reserved_names_.end(), lowercased) == reserved_names_.end());
            if (!valid)
                set(Optional_Fields::warning, "Some parameters are missing from this log message because they were malformed.");

            return valid;
        }

        bool is_valid(rapidjson::Document& param);

        template <typename T> Message& add(const char* param_name, T param)
        {
            std::string trimmed(param_name);
            trim(trimmed);

            rapidjson::GenericValue<T> v(param);

            if (param_name && is_valid(trimmed.c_str(), param))
            {
                auto it(msg_.FindMember(trimmed.c_str()));
                if (it == msg_.MemberEnd())
                    msg_.AddMember(trimmed.c_str(), v, msg_.GetAllocator());
                else
                    *it = v;
            }

            return *this;
        }
        
        Message& set_sequence(unsigned long long int sequence);

        rapidjson::Document msg_;
        bool validate_params_;

        static std::vector<std::string> reserved_names_;
        static void one_time_init();
};

class FPLOG_API File
{
    public:

        File(const char* prio, const char* name, const void* content, size_t size);
        Message as_message(){ return msg_; }


    private:

        File();
        File(File&);

        Message msg_;
        char* buf_;
};

class FPLOG_API Filter_Base
{
    public:

        Filter_Base(const char* filter_id) { if (filter_id) filter_id_ = filter_id; else filter_id_ = ""; }
        virtual bool should_pass(const Message& msg) = 0;
        std::string get_id(){ std::lock_guard<std::recursive_mutex> lock(mutex_); std::string id(filter_id_); return id; };
        virtual ~Filter_Base() {}


    protected:

        std::string filter_id_; //just a human-readable name for the filter
        std::recursive_mutex mutex_;


    private:

        Filter_Base();
};

//You need to explicitly state messages of which priorities you need to log by using add/remove.
class FPLOG_API Priority_Filter: public Filter_Base
{
    public:

        Priority_Filter(const char* filter_id): Filter_Base(filter_id){ construct_numeric(); }
        virtual ~Priority_Filter() {}

        virtual bool should_pass(const Message& msg);

        void add(const char* prio){ if (prio) prio_.insert(prio); }
        void remove(const char* prio) { if (prio){ std::set<std::string>::iterator it(prio_.find(prio)); if (it != prio_.end()) { prio_.erase(it); }}}

        void add_all_above(const char* prio, bool inclusive = false);
        void add_all_below(const char* prio, bool inclusive = false);


    private:

        std::set<std::string> prio_;
        std::vector<std::string> prio_numeric_;
        
        void construct_numeric();
};

//One time per application call.
//async_logging means that log messages are going to the queue before dispatching to the destination.
//This process is faster than sync logging but it also means that if app crashes with some messages still
//in the queue, those messages are lost. If you need to debug some app crash, set this parameter to false
//until you find the reason for the crash.
FPLOG_API void initlog(const char* appname, const char* uid, sprot::Basic_Transport_Interface* transport = 0, bool async_logging = true);

//One time per application call to stop logging from an application and free all associated resources.
FPLOG_API void shutdownlog();

//Mandatory call from every thread that wants to log some data. Done to increase flexibility:
//each thread will have its own filters configuration and can decide independently which stuff to log.
//Each filter within one thread must have unique id.
FPLOG_API void openlog(const char* facility, Filter_Base* filter = 0);

//Optional but advised to call from each thread that logs data right before stopping and exiting the thread.
//Doing this will free some memory (amount is really small) that is needed to support logging from a given thread.
//Not doing closelog() does not lead to memory leak because everything will be deallocated in any case on app exit,
//fplog frees its resources when implementation class instance is destroyed.
FPLOG_API void closelog();

//Scope of filter-related functions is a calling thread - all manipulations will apply to calling thread only.
//fplog will take ownership of the filter on adding and will deallocate it on removing, closelog() or shutdownlog().
FPLOG_API void add_filter(Filter_Base* filter);
FPLOG_API void remove_filter(Filter_Base* filter);
FPLOG_API Filter_Base* find_filter(const char* filter_id);

FPLOG_API const char* get_facility();

//Should be used from any thread that opened logger, calling from other threads will have no effect.
FPLOG_API void write(const Message& msg);

FPLOG_API void change_config(const sprot::Params& config);

};
