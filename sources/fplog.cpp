#include <fplog.h>
#include <utils.h>
#include <map>
#include <thread>
#include <queue>
#include <sprot.h>
#include <udp_transport.h>
#include <mutex>
#include <queue_controller.h>
#include <rapidjson/rapidjson.h>
#include <rapidjson/allocators.h>
#include <rapidjson/writer.h>
#include <stdarg.h>
#include <piped_sequence.h>

namespace fplog
{

typedef std::map<std::string, std::shared_ptr<Filter_Base>> Filter_Map;

const char* Prio::emergency = "emergency"; //system is unusable
const char* Prio::alert = "alert"; //action must be taken immediately
const char* Prio::critical = "critical"; //critical conditions
const char* Prio::error = "error"; //error conditions
const char* Prio::warning = "warning"; //warning conditions
const char* Prio::notice = "notice"; //normal but significant condition
const char* Prio::info = "info"; //informational
const char* Prio::debug = "debug"; //debug/trace info for developers

const char* Facility::system = "system"; //message from some system component
const char* Facility::user = "user"; //message from user-level component
const char* Facility::security = "security"; //security or authorization related message
const char* Facility::fplog = "fplog"; //message from fplog directly, could inform about log system malfunction

const char* Message::Mandatory_Fields::facility = "facility"; //according to syslog
const char* Message::Mandatory_Fields::priority = "priority"; //same as severity for syslog
const char* Message::Mandatory_Fields::timestamp = "timestamp"; //ISO8601 timestamp with milliseconds and timezone
const char* Message::Mandatory_Fields::hostname = "hostname"; //IP address or any specific sending device id, added by fplogd before sending
const char* Message::Mandatory_Fields::appname = "appname"; //name of application or service using this logging library, needed for fplog IPC

const char* Message::Optional_Fields::text = "text"; //log message text
const char* Message::Optional_Fields::component = "component"; //package name or any logical software component
const char* Message::Optional_Fields::class_name = "class"; //class name if OOP is used
const char* Message::Optional_Fields::method = "method"; //method of a given class if OOP is used or just a function name
const char* Message::Optional_Fields::module = "module"; //source file name
const char* Message::Optional_Fields::line = "line"; //line number in the above mentioned source file
const char* Message::Optional_Fields::options = "options"; //for example encryption method + options when encryption is in use
const char* Message::Optional_Fields::encrypted = "encrypted"; //true/false, if true then Text field contains encrypted JSON values - 
                                                               //the rest of the log message including the decrypted version of Text field
const char* Message::Optional_Fields::file = "file"; //filename when sending a file inside the log message
const char* Message::Optional_Fields::/*the*/blob = "blob"; //used when attaching binary fields to the message, resulting JSON object will look
                                                            //like this: "blob_name":{ "blob":"xckjhKJSHDKDSdJKShdsdsgr=" }
                                                            //where "xckjhKJSHDKDSdJKShdsdsgr=" is base64 encoded binary object
const char* Message::Optional_Fields::warning = "warning"; //contains warning for the user in case there was an issue with this specific log message
const char* Message::Optional_Fields::sequence = "sequence"; //sequence number that allows to prevent duplicate messages and also to tell
                                                             //which message was first even if timestamps are the same
const char* Message::Optional_Fields::batch = "batch"; //indicator if this message is actually a container for N other shorter messages

Message::Message(const char* prio, const char *facility, const char* format, ...):
msg_()
{
    set_timestamp();
    set(Mandatory_Fields::priority, prio ? prio : Prio::debug);
    set(Mandatory_Fields::facility, facility ? facility : Facility::user);

    if (format)
    {
        va_list aptr;
        va_start(aptr, format);

        char buffer[2048] = {0};
        vsnprintf(buffer, sizeof(buffer) - 1, format, aptr);
        set_text(buffer);

        va_end(aptr);
    }
}

Message& Message::set_module(std::string& module)
{
    return set(Optional_Fields::module, module);
}

Message& Message::set_module(const char* module)
{
    return set(Optional_Fields::module, module);
}

Message& Message::set_line(int line)
{
    return set(Optional_Fields::line, line);
}

Message& Message::set_text(std::string& text)
{
    return set(Optional_Fields::text, text);
}

Message& Message::set_text(const char* text)
{
    return set(Optional_Fields::text, text);
}

Message& Message::set_class(std::string& class_name)
{
    return set(Optional_Fields::class_name, class_name);
}

Message& Message::set_class(const char* class_name)
{
    return set(Optional_Fields::class_name, class_name);
}

Message& Message::set_method(std::string& method)
{
    return set(Optional_Fields::method, method);
}

Message& Message::set_method(const char* method)
{
    return set(Optional_Fields::method, method);
}

Message& Message::set_sequence(unsigned long long int sequence)
{
    return set(Optional_Fields::sequence, sequence);
}

Message& Message::add(rapidjson::Document& param)
{
    if (is_valid(param))
    {
        auto it(msg_.FindMember(param.MemberBegin()->name));
        if (it != msg_.MemberEnd())
            it->value.CopyFrom(param.MemberBegin()->value, msg_.GetAllocator());
        else
            msg_.AddMember(param.MemberBegin()->name, param.MemberBegin()->value, msg_.GetAllocator());
    }

    return *this;
}

Message& Message::add(const std::string& json)
{
    rapidjson::GenericDocument<rapidjson::UTF8<>> parsed;
    std::unique_ptr<char[]> to_parse(new char[json.size() + 1]);
    parsed.ParseInsitu(to_parse.get());

    return add(parsed);
}

Message& Message::add_batch(rapidjson::Document& batch)
{
    auto it(batch.MemberBegin());

    if ((it == batch.MemberEnd()) || !it->value.IsArray())
        THROW(fplog::exceptions::Incorrect_Parameter);

    it->name.SetString(fplog::Message::Optional_Fields::batch, static_cast<unsigned int>(strlen(fplog::Message::Optional_Fields::batch)));

    validate_params_ = false;

    try
    {
        add(batch);
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

    return *this;
}

bool Message::has_batch()
{
    for (auto it(msg_.MemberBegin()); it != msg_.MemberEnd(); ++it)
    {
        if (it->value.IsArray() && (strcmp(it->name.GetString(), fplog::Message::Optional_Fields::batch)) == 0)
            return true;
    }

    return false;
}

rapidjson::Document Message::get_batch()
{
    auto it(msg_.FindMember(fplog::Message::Optional_Fields::batch));

    rapidjson::Document d;
    d.SetObject();

    if (it != msg_.MemberEnd())
        d.AddMember(it->name, it->value, d.GetAllocator());

    return d;
}

bool Message::is_valid(rapidjson::Document& param)
{
    if (!validate_params_)
        return true;

    for(auto it(param.MemberBegin()); it != param.MemberEnd(); ++it)
    {
        rapidjson::Document d;
        d.AddMember(it->name, it->value, d.GetAllocator());

        if (!is_valid(d))
            return false;

        std::string lowercased(it->name.GetString());
        generic_util::trim(lowercased);
        std::transform(lowercased.begin(), lowercased.end(), lowercased.begin(), ::tolower);

        bool invalid(std::find(reserved_names_.begin(), reserved_names_.end(), lowercased) != reserved_names_.end());
        if (invalid)
        {
            set(Optional_Fields::warning, "Some parameters are missing from this log message because they were malformed.");
            return false;
        }
    }

    return true;
}

std::string Message::as_string() const
{
    rapidjson::StringBuffer s;
    rapidjson::Writer<rapidjson::StringBuffer> w(s);
    msg_.Accept(w);
    return s.GetString();
}

rapidjson::Document& Message::as_json()
{
    return msg_;
}

Message::Message(const rapidjson::Document& msg)
{
    msg_.CopyFrom(msg, msg_.GetAllocator());
}

Message::Message(const std::string& msg)
{
    msg_.Parse(msg.c_str());
}

Message& Message::add(const char* param_name, std::string& param)
{
    std::string trimmed(param_name);
    trim(trimmed);

    rapidjson::GenericValue<rapidjson::UTF8<char>, rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>> v(param.c_str(), msg_.GetAllocator());
    rapidjson::GenericValue<rapidjson::UTF8<char>, rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>> name(trimmed.c_str(), msg_.GetAllocator());

    if (param_name && is_valid(trimmed.c_str(), param))
    {
        auto it(msg_.FindMember(trimmed.c_str()));
        if (it == msg_.MemberEnd())
            msg_.AddMember(name, v, msg_.GetAllocator());
        else
            it->value = v;
    }

    return *this;
}

Message& Message::add(const char* param_name, const char* param)
{
    std::string trimmed(param_name);
    trim(trimmed);

    rapidjson::GenericValue<rapidjson::UTF8<>, rapidjson::MemoryPoolAllocator<>> v(param, msg_.GetAllocator());
    rapidjson::GenericValue<rapidjson::UTF8<>, rapidjson::MemoryPoolAllocator<>> name(trimmed.c_str(), msg_.GetAllocator());

    if (param_name && is_valid(trimmed.c_str(), param))
    {
        auto it(msg_.FindMember(trimmed.c_str()));
        if (it == msg_.MemberEnd())
            msg_.AddMember(name, v, msg_.GetAllocator());
        else
            it->value = v;
    }

    return *this;
}

bool Priority_Filter::should_pass(const Message& msg)
{
    Message& m = const_cast<Message&>(msg);
    auto it(m.as_json().FindMember(fplog::Message::Mandatory_Fields::priority));

    if (it != m.as_json().MemberEnd())
        return (prio_.find(it->value.GetString()) != prio_.end());

    return false;
}

void Priority_Filter::construct_numeric()
{
    prio_numeric_.push_back(Prio::emergency);
    prio_numeric_.push_back(Prio::alert);
    prio_numeric_.push_back(Prio::critical);
    prio_numeric_.push_back(Prio::error);
    prio_numeric_.push_back(Prio::warning);
    prio_numeric_.push_back(Prio::notice);
    prio_numeric_.push_back(Prio::info);
    prio_numeric_.push_back(Prio::debug);
}

void Priority_Filter::add_all_above(const char* prio, bool inclusive)
{
    //std::cout << "--> add_all_above(" << prio << ")" << std::endl;
    std::vector<std::string>::reverse_iterator it(prio_numeric_.rbegin());
    
    for (; it != prio_numeric_.rend(); ++it)
    {
        //cout << "it = " << *it << std::endl;
        if (it->find(std::string(prio)) != std::string::npos)
            break;
    }

    if (it == prio_numeric_.rend())
    {
        //std::cout << "prio not found!!!" << std::endl;
        return;
    }
    
    prio_.clear();

    if (inclusive)
        prio_.insert(prio);

    ++it;

    for (; it != prio_numeric_.rend(); ++it)
    {
        prio_.insert(*it);
    }
}

void Priority_Filter::add_all_below(const char* prio, bool inclusive)
{
    std::vector<std::string>::iterator it(prio_numeric_.begin());

    for (; it != prio_numeric_.end(); ++it)
        if (it->find(std::string(prio)) != std::string::npos)
            break;

    if (it == prio_numeric_.end())
        return;

    prio_.clear();

    if (inclusive)
        prio_.insert(prio);

    ++it;

    for (; it != prio_numeric_.end(); ++it)
    {
        prio_.insert(*it);
    }
}

Message& Message::set_timestamp(const char* timestamp)
{
    if (timestamp)
        return set(Mandatory_Fields::timestamp, timestamp);

    return set(Mandatory_Fields::timestamp, generic_util::get_iso8601_timestamp().c_str());
}

Message& Message::set_file(const char* name)
{
    if (name)
        return set(Optional_Fields::file, name);

    return *this;
}

File::File(const char* prio, const char* name, const void* content, size_t size):
msg_(prio, Facility::user)
{
    if (!name)
        return;

    msg_.set_file(name);

    if (size > 0)
    {
        size_t dest_len = generic_util::base64_encoded_length(size);
        buf_ = new char [dest_len + 1];
        memset(buf_, 0, dest_len + 1);
        generic_util::base64_encode(content, size, buf_, dest_len);

        msg_.set_text(buf_);

        delete [] buf_;
    }
}

Message& Message::add_binary(const char* param_name, const void* buf, size_t buf_size_bytes)
{
    if (!param_name || !buf || !buf_size_bytes)
        return *this;

    rapidjson::Document d;
    rapidjson::Document value;

    d.SetObject();
    value.SetObject();

    size_t dest_len = generic_util::base64_encoded_length(buf_size_bytes);
    char* base64 = new char [dest_len + 1];
    memset(base64, 0, dest_len + 1);
    generic_util::base64_encode(buf, buf_size_bytes, base64, dest_len);

    value.AddMember(rapidjson::StringRef("blob"), rapidjson::StringRef(base64), value.GetAllocator());

    d.AddMember(rapidjson::StringRef(param_name), value, d.GetAllocator());

    delete [] base64;

    validate_params_ = false;

    try
    {
        add(d);
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

    return *this;
}

void Message::one_time_init()
{
    reserved_names_.push_back(Mandatory_Fields::appname);
    reserved_names_.push_back(Mandatory_Fields::facility);
    reserved_names_.push_back(Mandatory_Fields::hostname);
    reserved_names_.push_back(Mandatory_Fields::priority);
    reserved_names_.push_back(Mandatory_Fields::timestamp);

    reserved_names_.push_back(Optional_Fields::blob);
    reserved_names_.push_back(Optional_Fields::class_name);
    reserved_names_.push_back(Optional_Fields::component);
    reserved_names_.push_back(Optional_Fields::encrypted);
    reserved_names_.push_back(Optional_Fields::file);
    reserved_names_.push_back(Optional_Fields::method);
    reserved_names_.push_back(Optional_Fields::line);
    reserved_names_.push_back(Optional_Fields::module);
    reserved_names_.push_back(Optional_Fields::options);
    reserved_names_.push_back(Optional_Fields::text);
    reserved_names_.push_back(Optional_Fields::warning);
    reserved_names_.push_back(Optional_Fields::sequence);
    reserved_names_.push_back(Optional_Fields::batch);
}

std::vector<std::string> Message::reserved_names_;

/************************* fplog client API implementation *************************/

FPLOG_API std::vector<std::string> g_test_results_vector;

class FPLOG_API Fplog_Impl
{
    public:

        Fplog_Impl():
        appname_("noname"),
        inited_(false),
        own_transport_(true),
        test_mode_(false),
        stopping_(false),
        transport_(0),
        mq_reader_(0),
        async_logging_(true)
        {
            Message::one_time_init();
        }

        ~Fplog_Impl()
        {
            stop_reading_queue();
            mq_reader_->join();

            std::lock_guard<std::recursive_mutex> lock(mutex_);

            delete mq_reader_;
            delete protocol_;

            if (inited_ && own_transport_)
                delete transport_;
        }

        const char* get_facility()
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            return thread_log_settings_table_[std::hash<std::thread::id>()(std::this_thread::get_id())].facility_.c_str();
        }

        void openlog(const char* facility, Filter_Base* filter)
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);

            if (facility)
                thread_log_settings_table_[std::hash<std::thread::id>()(std::this_thread::get_id())].facility_ = facility;
            else
                return;

            if (filter)
                add_filter(filter);
        }

        void initlog(const char* appname, sprot::Basic_Transport_Interface* transport, bool async_logging)
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);

            async_logging_ = async_logging;

            if (!mq_reader_)
                mq_reader_ = new std::thread(&Fplog_Impl::mq_reader, this);
            
            if (inited_)
                return;

            if (appname)
                appname_ = appname;
            else
                return;

            if (transport)
            {
                own_transport_ = false;
                transport_ = transport;
            }
            else
            {
                /*own_transport_ = true;
                transport_ = new spipc::Socket_Transport();

                fplog::Transport_Interface::Params params;
                params["uid"] = uid;
                params["ip"] = "127.0.0.1";

                transport_->connect(params);
                protocol_ = new sprot::Protocol(transport_);

                inited_ = true;*/
            }
        }

        void closelog()
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);

            std::map<unsigned long long int, Logger_Settings>::iterator it = thread_log_settings_table_.find(std::hash<std::thread::id>()(std::this_thread::get_id()));
            if (it != thread_log_settings_table_.end())
                thread_log_settings_table_.erase(it);
        }

        static std::string strip_timestamp_and_sequence(std::string input)
        {
            generic_util::remove_json_field(fplog::Message::Mandatory_Fields::timestamp, input);
            generic_util::remove_json_field(fplog::Message::Optional_Fields::sequence, input);

            return input;
        }

        void write(const Message& m)
        {
            Message msg(m);

            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (stopping_)
                return;

            msg.set(Message::Mandatory_Fields::appname, appname_);
            //std::cout << "logging message: " << msg.as_string() << std::endl;
            
            if (passed_filters(msg))
            {
                //std::cout << "message passed filters OK" << std::endl;
                msg.set_sequence(sequence_number::read_sequence_number());

                if (test_mode_)
                    g_test_results_vector.push_back(strip_timestamp_and_sequence(msg.as_string()));
                else
                {
                    if (async_logging_)
                    {
                        //std::cout << "message got inside the queue" << std::endl;
                        mq_.push(new std::string(msg.as_string()));
                    }
                    else
                    {
                        int send_retries = 12;
                        while (send_retries > 0)
                        {
                            try
                            {
                                //std::cout << "preparing to write message directly" << std::endl;
                                std::string str(msg.as_string());
                                protocol_->write(str.c_str(), str.size(), 400);
                                break;
                            }
                            catch(fplog::exceptions::Generic_Exception& e)
                            {
                                //std::cout << "message not sent, err = " << e.what() << std::endl;
                                send_retries--;
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            }
                        }
                    }
                }
            }
        }

        void add_filter(Filter_Base* filter)
        {
            if (!filter)
                return;

            std::string filter_id(filter->get_id());
            generic_util::trim(filter_id);
            if (filter_id.empty())
                return;

            std::lock_guard<std::recursive_mutex> lock(mutex_);
            Logger_Settings settings = Logger_Settings(thread_log_settings_table_[std::hash<std::thread::id>()(std::this_thread::get_id())]);
            settings.filter_id_ptr_map[filter_id] = std::shared_ptr<Filter_Base>(filter);

            thread_log_settings_table_[std::hash<std::thread::id>()(std::this_thread::get_id())] = settings;
        }

        void remove_filter(Filter_Base* filter)
        {
            if (!filter)
                return;

            std::lock_guard<std::recursive_mutex> lock(mutex_);

            Logger_Settings settings(thread_log_settings_table_[std::hash<std::thread::id>()(std::this_thread::get_id())]);

            std::string filter_id(filter->get_id());
            if (!filter_id.empty())
            {
                settings.filter_id_ptr_map.erase(settings.filter_id_ptr_map.find(filter_id));
                thread_log_settings_table_[std::hash<std::thread::id>()(std::this_thread::get_id())] = settings;
            }
        }

        Filter_Base* find_filter(const char* filter_id)
        {
            if (!filter_id)
                return 0;

            std::string filter_id_trimmed(filter_id);
            generic_util::trim(filter_id_trimmed);
            if (filter_id_trimmed.empty())
                return 0;

            std::lock_guard<std::recursive_mutex> lock(mutex_);

            Logger_Settings settings(thread_log_settings_table_[std::hash<std::thread::id>()(std::this_thread::get_id())]);
            std::map<std::string, std::shared_ptr<Filter_Base>>::iterator found(settings.filter_id_ptr_map.find(filter_id_trimmed));
            if (found != settings.filter_id_ptr_map.end())
                return found->second.get();

            return 0;
        }

        void set_test_mode(bool mode);
        void wait_until_queues_are_empty();
        void change_config(const sprot::Params& config);


    private:

        //Shared_Sequence_Number sequence_;
        bool inited_;
        bool own_transport_;
        bool test_mode_;

        volatile bool stopping_;
        bool async_logging_;

        std::string appname_;

        Queue_Controller mq_;
        std::thread* mq_reader_;

        struct Logger_Settings
        {
            Logger_Settings() : facility_(Facility::user) {}
            Filter_Map filter_id_ptr_map;
            std::string facility_;
        };

        std::map<unsigned long long int, Logger_Settings> thread_log_settings_table_;

        std::recursive_mutex mutex_;
        std::recursive_mutex mq_reader_mutex_;

        sprot::Basic_Transport_Interface* transport_;
        sprot::Basic_Transport_Interface* protocol_;

        void stop_reading_queue()
        {
            stopping_ = true;
            std::lock_guard<std::recursive_mutex> lock(mq_reader_mutex_);
        }
        
        void mq_reader()
        {
            std::lock_guard<std::recursive_mutex> queue_lock(mq_reader_mutex_);

            while(!stopping_)
            {
                
                std::string* str = 0;

                {
                    std::lock_guard<std::recursive_mutex> lock(mutex_);
                
                    if (!mq_.empty() && transport_)
                    {
                        str = mq_.front();
                        mq_.pop();
                    }
                }

                std::unique_ptr<std::string> str_ptr(str);

            retry:

                if (stopping_)
                    return;

                try
                {
                    if (str)
                        protocol_->write(str->c_str(), str->size(), 400);
                    else
                    {
                        if (stopping_)
                            return;
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }

                }
                catch(fplog::exceptions::Generic_Exception)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    goto retry;
                }
            }
        }

        bool passed_filters(const Message& msg)
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            Logger_Settings settings(thread_log_settings_table_[std::hash<std::thread::id>()(std::this_thread::get_id())]);
            
            //std::cout << "--> passed_filters" << std::endl;
            
            if (settings.filter_id_ptr_map.size() == 0)
            {
                //std::cout << "FALSE <-- passed_filters (no filters in map)" << std::endl;
                return false;
            }

            bool should_pass = true;

            for (std::map<std::string, std::shared_ptr<Filter_Base>>::iterator it = settings.filter_id_ptr_map.begin(); it != settings.filter_id_ptr_map.end(); ++it)
            {
                should_pass = (should_pass && it->second->should_pass(msg));
                if (!should_pass)
                    break;
            }

            //std::cout << should_pass << " <-- passed_filters" << std::endl;
            return should_pass;
        }
};

FPLOG_API Fplog_Impl* g_fplog_impl = 0;
std::recursive_mutex g_api_mutex;

void write(const Message& msg)
{
    std::lock_guard<std::recursive_mutex> lock(g_api_mutex);
    
    if (!g_fplog_impl)
        return;
   
    g_fplog_impl->write(msg);
}

void initlog(const char* appname, const char* uid, sprot::Basic_Transport_Interface* transport, bool async_logging)
{
    std::lock_guard<std::recursive_mutex> lock(g_api_mutex);

    if (!g_fplog_impl)
        g_fplog_impl = new Fplog_Impl();

    return g_fplog_impl->initlog(appname, uid, transport, async_logging);
}

void shutdownlog()
{
    std::lock_guard<std::recursive_mutex> lock(g_api_mutex);

    delete g_fplog_impl;
    g_fplog_impl = 0;
}

void openlog(const char* facility, Filter_Base* filter)
{
    std::lock_guard<std::recursive_mutex> lock(g_api_mutex);

    if (!g_fplog_impl)
        return;

    return g_fplog_impl->openlog(facility, filter);
}

void closelog()
{
    std::lock_guard<std::recursive_mutex> lock(g_api_mutex);

    if (!g_fplog_impl)
        return;

    return g_fplog_impl->closelog();
}

const char* get_facility()
{
    std::lock_guard<std::recursive_mutex> lock(g_api_mutex);

    if (!g_fplog_impl)
        return "";

    return g_fplog_impl->get_facility();
}

void add_filter(Filter_Base* filter)
{
    std::lock_guard<std::recursive_mutex> lock(g_api_mutex);

    if (!g_fplog_impl)
        return;

    return g_fplog_impl->add_filter(filter);
}

void remove_filter(Filter_Base* filter)
{
    std::lock_guard<std::recursive_mutex> lock(g_api_mutex);

    if (!g_fplog_impl)
        return;

    return g_fplog_impl->remove_filter(filter);
}

Filter_Base* find_filter(const char* filter_id)
{
    std::lock_guard<std::recursive_mutex> lock(g_api_mutex);

    if (!g_fplog_impl)
        return 0;

    return g_fplog_impl->find_filter(filter_id);
}

void change_config(const sprot::Params& config)
{
    std::lock_guard<std::recursive_mutex> lock(g_api_mutex);

    if (!g_fplog_impl)
        return;

    return g_fplog_impl->change_config(config);
}

FPLOG_API void Fplog_Impl::set_test_mode(bool mode)
{
    g_test_results_vector.clear(); test_mode_ = mode;
}

FPLOG_API void Fplog_Impl::wait_until_queues_are_empty()
{
    int counter = 0;

    while (counter < 5)
    {
        bool q1_empty = true;

        {
            std::lock_guard<std::recursive_mutex> lock_q1(mutex_);
            q1_empty = mq_.empty();
        }

        if (q1_empty)
        {
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        else
        {
            counter = 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

FPLOG_API void Fplog_Impl::change_config(const sprot::Params& config)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    mq_.apply_config(config);
}

#ifdef __linux__
#include <cxxabi.h>

FPLOG_API ::std::string demangle_cpp_name(const char* mangled)
{
    int     status;
    char   *realname = abi::__cxa_demangle(mangled, 0, 0, &status);
    ::std::string res(realname);
    free(realname);

    return res;
}
#endif

#ifdef __APPLE__
#include <cxxabi.h>

FPLOG_API ::std::string demangle_cpp_name(const char* mangled)
{
    int     status;
    char   *realname = abi::__cxa_demangle(mangled, 0, 0, &status);
    ::std::string res(realname);
    free(realname);

    return res;
}
#endif

};
