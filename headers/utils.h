#pragma once

#include <string>
#include <algorithm> 
#include <functional> 
#include <cctype>
#include <locale>
#include <algorithm>
#include <vector>
#include <fstream>
#include <queue>
#include <thread>
#include <mutex>

template<typename T>
struct std::hash<std::vector<T>>
{
    typedef vector<T> argument_type;
    typedef std::size_t result_type;
    result_type operator()(argument_type const& in) const
    {
        size_t size = in.size();
        size_t seed = 0;
        for (size_t i = 0; i < size; i++)
            //Combine the hash of the current vector with the hashes of the previous ones
            hash_combine(seed, in[i]);
        return seed;
    }
};

//using boost::hash_combine
template <class T>
inline void hash_combine(std::size_t& seed, T const& v)
{
    seed ^= std::hash<T>()(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace debug_logging
{
    class Logger
    {
        public:

            Logger();
            ~Logger();

            bool open(const char* file, bool unbuffered = true);
            void log(const char* func, const char* file, const int line, const char* msg);
            void log(const std::string& message);


        private:

            std::queue<std::string> messages_;
            std::recursive_mutex mutex_;
            std::thread writer_;

            static void writer_thread(Logger* l);
            bool shutdown_ = false;

            FILE* log_ = nullptr;
    };

    extern Logger g_logger;
}

namespace generic_util
{

struct Retryable
{
    using Retry_Function = std::function<bool()>;
    unsigned int max_retries = 0;
    unsigned int actual_retries = 0;

    Retryable(Retry_Function func, unsigned int retries_allowed): max_retries(retries_allowed), retryable_function { std::move(func) } {}
    bool run()
    {
        for (; actual_retries < max_retries; actual_retries++)
            if (retryable_function())
                break;

        return (actual_retries < max_retries);
    }

    Retry_Function retryable_function;
};

uint16_t gen_crc16(const uint8_t *data, uint16_t size);

template<typename InputIterator1, typename InputIterator2>
bool
range_equal(InputIterator1 first1, InputIterator1 last1,
        InputIterator2 first2, InputIterator2 last2)
{
    while(first1 != last1 && first2 != last2)
    {
        if(*first1 != *first2) return false;
        ++first1;
        ++first2;
    }
    return (first1 == last1) && (first2 == last2);
}

bool compare_files(const std::string& filename1, const std::string& filename2);

//using boost::hash_combine
template <class T>
inline void hash_combine(std::size_t& seed, T const& v)
{
    seed ^= std::hash<T>()(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

std::string& remove_json_field(const char* field_name, std::string& source);

void process_suicide(size_t timeout, int signal = 15);
void suicide_prevention();
    
static inline bool compare_no_case(char ch1, char ch2){ return std::toupper(ch1) == std::toupper(ch2); }
static inline bool find_str_no_case(const std::string& search_where, const std::string& search_what)
{
    return (std::search(search_where.begin(), search_where.end(), search_what.begin(), search_what.end(), compare_no_case) != search_where.end());
}

//Returns difference in minutes from UTC
//Example: for UTC+3 function returns 180
int get_system_timezone();

//Converts timezone bias in minutes (returned by get_system_timezone() for example) to iso8601 representation
std::string timezone_from_minutes_to_iso8601(int tz_minute_bias);

//Returns current local date-time in iso 8601 format including timezone information
std::string get_iso8601_timestamp();

//Milliseconds elapsed since 01-Jan-1970
unsigned long long get_msec_time();

bool base64_encode(const void* source, size_t sourcelen, char* dest, size_t destlen);
size_t base64_decode(const char* source, void* dest, size_t destlen);
size_t base64_encoded_length(size_t non_encoded_length);

// trim from start
static inline std::string &ltrim(std::string &s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::function<int(int)>(isspace))));
        return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::function<int (int)>(isspace))).base(), s.end());
        return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
        return ltrim(rtrim(s));
}

std::vector<std::string> tokenize(const char *str, char c = ' ');

};
