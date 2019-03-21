#include <utils.h>

#include <stdio.h>
#include <mutex>
#include <string>
#include <cstring>
#include <thread>
#include <chrono>
#include <iostream>

#define CRC16 0x8005
using namespace std::chrono;

#ifdef _WIN32
#include <windows.h>

static int get_system_timezone_impl()
{
    TIME_ZONE_INFORMATION tz = {0};
    unsigned int res = GetTimeZoneInformation(&tz);

    if ((res == 0) || (res == TIME_ZONE_ID_INVALID)) return 0;
    if (res == 2) return -1 * (tz.DaylightBias + tz.Bias);
    
    return -1 * tz.Bias;
}

static unsigned long long get_msec_time_impl()
{
    __time64_t elapsed_seconds(_time64(0));

    unsigned long long result = elapsed_seconds > 0 ? elapsed_seconds * 1000 : 0;

    if (result == 0)
        return result;

    SYSTEMTIME st = {0};
    GetSystemTime(&st);
    result += st.wMilliseconds;

    return result;
}

#define snprintf _snprintf

#else

#include <date/date.h>

static int get_system_timezone_impl()
{
    FILE* tz = nullptr;

    #ifdef __APPLE__
	char cmd[] = "gdate +%:z";
    #else
	char cmd[] = "date +%:z";
    #endif

    char line[256];
    memset(line, 0, sizeof(line));

    tz = popen(cmd, "r");
        
    if (tz != nullptr)
    {
        if (fgets(line, 256, tz))
        {
            pclose(tz);
            
            char hours[256];
            char minutes[256];
            
            memset(hours, 0, sizeof(hours));
            memset(minutes, 0, sizeof(minutes));
            
            int separator = -1;
            for (int i = 0; i < sizeof(line); ++i)
                if (line[i] == ':')
                {
                    separator = i;
                    break;
                }

            if (separator > 0)
            {
                int len = strlen(line);
                if (len > 0)
                {
                    memcpy(hours, line, separator);
                    memcpy(minutes, line + separator + 1, len - separator - 1);
                    
                    int h = std::stoi(hours), m = std::stoi(minutes);
                    
                    if (h >= 0)
                        return 60 * h + m;
                    else
                        return 60 * h - m;
                }
            }
        }
        else
            pclose(tz);
    }

    return 0;
}

static unsigned long long get_msec_time_impl()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

#endif

static bool prevent_suicide = false;
static std::recursive_mutex suicide_mutex;
static std::thread* suicide_thread = nullptr;

void cause_of_death(size_t timeout)
{
    time_point<system_clock, system_clock::duration> timer_start(system_clock::now());

    auto check_time_out = [&timeout, &timer_start]()
    {
        auto timer_start_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(timer_start);
        auto timer_stop_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        std::chrono::milliseconds timeout_ms(timeout);
        
        if (timer_stop_ms - timer_start_ms >= timeout_ms)
            throw std::out_of_range(std::string("Waited for ") + std::to_string(timeout) + std::string("ms, not all threads finished on time, had to terminate the process."));
    };

    try
    {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            {
                std::lock_guard<std::recursive_mutex> lock(suicide_mutex);
                if (prevent_suicide)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    return;
                }
            }

            check_time_out();
        }
    }
    catch(std::out_of_range&)
    {
        std::cout << "goodbye, cruel world" << std::endl;
        exit(EXIT_FAILURE);
    }
}

namespace debug_logging
{

    Logger::Logger():
    writer_(std::bind(Logger::writer_thread, this))
    {
    }

    Logger::~Logger()
    {
        shutdown_ = true;
        writer_.join();
    }

    void Logger::log(const char* func, const char* file, const int line, const char* msg)
    {
        if (!(func && file && msg))
            return;

        size_t len = strlen(func) + strlen(file) + strlen(msg);
        char str[2048];

        if (len > sizeof(str) - 16)
            return;

        sprintf(str, "in %s file @ %d, %s: %s\n", file, line, func, msg);
        log(str);
    }

    void Logger::log(const std::string& message)
    {
        if (log_)
        {
            char str[6000];

            if (message.length() > sizeof(str) - 16)
                return;

            sprintf(str, "thread#%21lu: %s", std::hash<std::thread::id>()(std::this_thread::get_id()), message.c_str());

            std::lock_guard lock(mutex_);
            messages_.push(str);
        }
    }

    bool Logger::open(const char* file, bool unbuffered)
    {
        if (!file)
            return false;

        std::lock_guard lock(mutex_);

        if (log_)
            fclose(log_);

        log_ = fopen(file, "wt");

        if (unbuffered)
            setbuf(log_, nullptr);

        return (log_ != nullptr);
    }

    void Logger::writer_thread(Logger* l)
    {
        while (!l->shutdown_)
        {
            if (!l->log_)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            std::queue<std::string> messages;

            {
                std::lock_guard lock(l->mutex_);
                messages = l->messages_;
                std::queue<std::string> empty;
                l->messages_.swap(empty);
            }

            while (messages.size() > 0)
            {
                std::string message(messages.front());
                fprintf(l->log_, "%s\n", message.c_str());
                messages.pop();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    Logger g_logger;
};

namespace generic_util
{

bool compare_files(const std::string& filename1, const std::string& filename2)
{
    std::ifstream file1(filename1);
    std::ifstream file2(filename2);

    std::istreambuf_iterator<char> begin1(file1);
    std::istreambuf_iterator<char> begin2(file2);

    std::istreambuf_iterator<char> end;

    return range_equal(begin1, end, begin2, end);
}

uint16_t gen_simple_crc16(const uint8_t *data, uint16_t size)
{
    //return gen_crc16(data, size);

    uint16_t op1 = 0;
    uint16_t op2 = 0;
    uint16_t ops_xor = 0;
    uint16_t total_xor = 0;

    int bytes_left = size;

    while (bytes_left > 0)
    {
        op1 = 0;
        op2 = 0;

        int copy_size = sizeof(op1);
        if (copy_size > bytes_left)
            copy_size = bytes_left;

        memcpy(&op1, data + size - bytes_left, static_cast<size_t>(copy_size));
        bytes_left -= copy_size;

        copy_size = sizeof(op2);
        if (copy_size > bytes_left)
            copy_size = bytes_left;

        memcpy(&op2, data + size - bytes_left, static_cast<size_t>(copy_size));
        bytes_left -= copy_size;

        ops_xor = op1 ^ op2;
        total_xor = total_xor ^ ops_xor;
    }

    return total_xor;
}

uint16_t gen_crc16(const uint8_t *data, uint16_t size)
{
    uint16_t out = 0;
    int bits_read = 0, bit_flag;

    /* Sanity check: */
    if(data == nullptr)
        return 0;

    while(size > 0)
    {
        bit_flag = out >> 15;

        /* Get next bit: */
        out <<= 1;
        out |= (*data >> bits_read) & 1; // item a) work from the least significant bits

        /* Increment bit counter: */
        bits_read++;
        if(bits_read > 7)
        {
            bits_read = 0;
            data++;
            size--;
        }

        /* Cycle check: */
        if(bit_flag)
            out ^= CRC16;

    }

    // item b) "push out" the last 16 bits
    int i;
    for (i = 0; i < 16; ++i) {
        bit_flag = out >> 15;
        out <<= 1;
        if(bit_flag)
            out ^= CRC16;
    }

    // item c) reverse the bits
    uint16_t crc = 0;
    i = 0x8000;
    int j = 0x0001;
    for (; i != 0; i >>=1, j <<= 1) {
        if (i & out) crc |= j;
    }

    return crc;
}

std::string& remove_json_field(const char* field_name, std::string& source)
{
    size_t found = source.find(field_name);
    if (found == std::string::npos)
        return source;
    
    std::string end_chars = ",}]\"'";
    
    size_t field_len = (std::string(field_name)).length();
    size_t pos1 = found - 1;
    size_t pos2 = pos1;
    
    for (auto separator : end_chars)
    {
        for (size_t c = found + field_len + 4; c < source.length(); c++)
            if (source[c] == separator)
            {
                pos2 = c;
                break;
            }
        
        if (pos2 != pos1)
            break;
    }

    if (source[pos1 - 1] == ',')
        pos1--;
    else
        if (source[pos2] == ',')
            pos2++;

    if (pos2 == pos1)
    {
        if (source.length() <= 0)
            return source;

        pos2 = source.length() - 1;
    }

    source.erase(pos1, pos2 - pos1);

    return remove_json_field(field_name, source);
}

void process_suicide(size_t timeout, int)
{
    std::lock_guard<std::recursive_mutex> lock(suicide_mutex);
    if (suicide_thread)
        return; //suicide in progress, do not interrupt (T_T)
    
    suicide_thread = new std::thread(cause_of_death, timeout);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

void suicide_prevention()
{
    if (!suicide_thread)
        return;

    {
        std::lock_guard<std::recursive_mutex> lock(suicide_mutex);
        prevent_suicide = true;
    }
    
    suicide_thread->join();
    
    delete suicide_thread;
    suicide_thread = nullptr;
    
    prevent_suicide = false;
}

unsigned long long get_msec_time() { return get_msec_time_impl(); }
int get_system_timezone() { return get_system_timezone_impl(); }

std::string timezone_from_minutes_to_iso8601(int tz_minute_bias)
{
    if (tz_minute_bias == 0) return "";

    int hours = tz_minute_bias / 60;
    int minutes = tz_minute_bias % 60;

    char str[25] = {0};
    snprintf(str, sizeof(str) - 1, "%s%02d%02d", hours > 0 ? "+" : "-", abs(hours), abs(minutes));

    return str;
}


#ifdef _WIN32

std::string get_iso8601_timestamp()
{
    time_t elapsed_time(time(0));
    struct tm* tm(localtime(&elapsed_time));
    char timestamp[200] = {0};

    static int tz = get_system_timezone();
    static int call_counter = 0;

    call_counter++;

    if (call_counter > 200)
    {
        call_counter = 0;
        tz = get_system_timezone();
    }

    snprintf(timestamp, sizeof(timestamp) - 1, "%04d-%02d-%02dT%02d:%02d:%02d.%03lld%s",
        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, get_msec_time() % 1000,
        timezone_from_minutes_to_iso8601(tz).c_str());

    return timestamp;
}
    
#else

std::string get_iso8601_timestamp()
{
    auto tp(std::chrono::system_clock::now());

    std::string tz_only(date::format("%z", tp));
    std::string datetime(date::format("%FT%T", tp));

    for (auto it(datetime.begin()); it != datetime.end(); it++)
    {
        if ((*it == '.') || (*it == ','))
        {
            it += 4;

            std::string res;
            res.append(datetime.begin(), it);
            res += tz_only;
            
            return res;
        }
    }

    return std::string("");
}

#endif

/**
 * characters used for Base64 encoding
 */  
const char *BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * encode three bytes using base64 (RFC 3548)
 *
 * @param triple three bytes that should be encoded
 * @param result buffer of four characters where the result is stored
 */  
void _base64_encode_triple(unsigned char triple[3], char result[4])
{
    int tripleValue, i;

    tripleValue = triple[0];
    tripleValue *= 256;
    tripleValue += triple[1];
    tripleValue *= 256;
    tripleValue += triple[2];

    for (i=0; i<4; i++)
    {
        result[3-i] = BASE64_CHARS[tripleValue%64];
        tripleValue /= 64;
    }
} 

/**
 * encode an array of bytes using Base64 (RFC 3548)
 *
 * @param source the source buffer
 * @param sourcelen the length of the source buffer
 * @param target the target buffer
 * @param targetlen the length of the target buffer
 * @return 1 on success, 0 otherwise
 */  
bool base64_encode(const void* src, size_t sourcelen, char* target, size_t targetlen)
 {
    unsigned char *source = (unsigned char*)src;

    /* check if the result will fit in the target buffer */
    if ((sourcelen+2)/3*4 > targetlen-1)
        return false;

    /* encode all full triples */
    while (sourcelen >= 3)
    {
        _base64_encode_triple(source, target);
        sourcelen -= 3;
        source += 3;
        target += 4;
    }

    /* encode the last one or two characters */
    if (sourcelen > 0)
    {
        unsigned char temp[3];
        memset(temp, 0, sizeof(temp));
        memcpy(temp, source, sourcelen);
        _base64_encode_triple(temp, target);

        target[3] = '=';
        if (sourcelen == 1)
            target[2] = '=';

        target += 4;
    }

    /* terminate the string */
    target[0] = 0;

    return true;
} 

/**
 * determine the value of a base64 encoding character
 *
 * @param base64char the character of which the value is searched
 * @return the value in case of success (0-63), -1 on failure
 */  
int _base64_char_value(char base64char)
{
    if (base64char >= 'A' && base64char <= 'Z')
        return base64char-'A';
    if (base64char >= 'a' && base64char <= 'z')
        return base64char-'a'+26;
    if (base64char >= '0' && base64char <= '9')
        return base64char-'0'+2*26;
    if (base64char == '+')
        return 2*26+10;
    if (base64char == '/')
        return 2*26+11;

    return -1;
} 

/**
 * decode a 4 char base64 encoded byte triple
 *
 * @param quadruple the 4 characters that should be decoded
 * @param result the decoded data
 * @return lenth of the result (1, 2 or 3), 0 on failure
 */  
int _base64_decode_triple(char quadruple[4], unsigned char *result)
{
    int i, triple_value, bytes_to_decode = 3, only_equals_yet = 1;
    int char_value[4];

    for (i=0; i<4; i++)
        char_value[i] = _base64_char_value(quadruple[i]);

    /* check if the characters are valid */
    for (i=3; i>=0; i--)
    {
        if (char_value[i]<0)
        {
            if (only_equals_yet && quadruple[i]=='=')
            {
                /* we will ignore this character anyway, make it something
                * that does not break our calculations */
                char_value[i]=0;
                bytes_to_decode--;
                continue;
            }

            return 0;
        }
        /* after we got a real character, no other '=' are allowed anymore */
        only_equals_yet = 0;
    }

    /* if we got "====" as input, bytes_to_decode is -1 */
    if (bytes_to_decode < 0)
        bytes_to_decode = 0;

    /* make one big value out of the partial values */
    triple_value = char_value[0];
    triple_value *= 64;
    triple_value += char_value[1];
    triple_value *= 64;
    triple_value += char_value[2];
    triple_value *= 64;
    triple_value += char_value[3];

    /* break the big value into bytes */
    for (i=bytes_to_decode; i<3; i++)
        triple_value /= 256;
    
    for (i=bytes_to_decode-1; i>=0; i--)
    {
        result[i] = triple_value%256;
        triple_value /= 256;
    }

    return bytes_to_decode;
} 

/**
 * decode base64 encoded data
 *
 * @param source the encoded data (zero terminated)
 * @param target pointer to the target buffer
 * @param targetlen length of the target buffer
 * @return length of converted data on success, -1 otherwise
 */  
size_t base64_decode(const char* source, void* dest, size_t targetlen)
 {
    unsigned char* target = (unsigned char*)dest;
    char *src, *tmpptr;
    char quadruple[4], tmpresult[3];
    int i, tmplen = 3;
    size_t converted = 0;

    /* concatinate '===' to the source to handle unpadded base64 data */
    src = (char *)malloc(strlen(source)+5);
    if (src == nullptr)
        return -1;
    
    strcpy(src, source);
    strcat(src, "====");
    tmpptr = src;

    /* convert as long as we get a full result */
    while (tmplen == 3)
    {
        /* get 4 characters to convert */
        for (i=0; i<4; i++)
        {
            /* skip invalid characters - we won't reach the end */
            while (*tmpptr != '=' && _base64_char_value(*tmpptr)<0)
                tmpptr++;

            quadruple[i] = *(tmpptr++);
        }

        /* convert the characters */
        tmplen = _base64_decode_triple(quadruple, (unsigned char*)tmpresult);

        /* check if the fit in the result buffer */
        if ((int)targetlen < tmplen)
        {
            free(src);
            return -1;
        }

        /* put the partial result in the result buffer */
        memcpy(target, tmpresult, tmplen);
        target += tmplen;
        targetlen -= tmplen;
        converted += tmplen;
    }

    free(src);
    return converted;
}

size_t base64_encoded_length(size_t non_encoded_length) { return (non_encoded_length+2)/3*4 + 1; }

std::vector<std::string> tokenize(const char *str, char c)
{
    std::vector<std::string> result;

    do
    {
        const char *begin = str;

        while(*str != c && *str)
            str++;

        result.push_back(std::string(begin, str));
    } while (0 != *str++);

    return result;
}

};
