#include <udp_transport.h>
#include <utils.h>
#include <chrono>

using namespace std::chrono;

class WSA_Up_Down
{
    public:

        WSA_Up_Down()
        {
            #ifndef __linux__
            #ifndef __APPLE__
            
			WSADATA wsaData;
            if (WSAStartup(0x202, &wsaData))
                THROW(fplog::exceptions::Connect_Failed);
			
			#endif
            #endif
        }

        ~WSA_Up_Down()
        {
            #ifndef __linux__
            #ifndef __APPLE__
			
            WSACleanup();
			
			#endif
            #endif
        }
};

namespace sprot {

void Udp_Transport::enable(const Params& params)
{
    static WSA_Up_Down sock_initer;

    std::lock_guard<std::recursive_mutex> lock_read(read_mutex_);
    std::lock_guard<std::recursive_mutex> lock_write(write_mutex_);

    localhost_ = true;
    auto get_ip = [this](const std::string& ip)
    {
        int count = 0;
        unsigned char addr[4];

        try
        {
            std::vector<std::string> tokens(generic_util::tokenize(ip.c_str(), '.'));
            for(auto it = tokens.begin(); it != tokens.end(); ++it)
            {
                if (count >= 4)
                    THROW(fplog::exceptions::Incorrect_Parameter);
                std::string token(*it);
                unsigned int byte = static_cast<unsigned int>(std::stoi(token));
                if (byte > 255)
                    THROW(fplog::exceptions::Incorrect_Parameter);
                addr[count] = static_cast<unsigned char>(byte);
                count++;
            }
        }
        catch(std::exception&)
        {
            THROW(fplog::exceptions::Incorrect_Parameter);
        }

        memcpy(ip_, addr, sizeof(ip_));
    };

    if (params.find("ip") != params.end())
    {
        get_ip(params.find("ip")->second);
        localhost_ = false;
    }

    if (params.find("IP") != params.end())
    {
        get_ip(params.find("IP")->second);
        localhost_ = false;
    }

    std::string port;

    if (params.find("port") != params.end())
    {
        port = params.find("port")->second;
    }

    if (params.find("PORT") != params.end())
    {
        port = params.find("PORT")->second;
    }

    std::string chaos_rate = "0";

    if (params.find("chaos") != params.end())
    {
        chaos_rate = params.find("chaos")->second;
    }

    if (params.find("CHAOS") != params.end())
    {
        chaos_rate = params.find("CHAOS")->second;
    }

    chaos_rate_ = std::stoi(chaos_rate);

    const unsigned char localhost[4] = {127, 0, 0, 1};
    if (memcmp(ip_, localhost, sizeof(localhost)) == 0)
        localhost_ = true;

    unsigned short iport = 0;

    try
    {
        iport = static_cast<unsigned short>(std::stoi(port));
    }
    catch(std::exception&)
    {
        THROW(fplog::exceptions::Incorrect_Parameter);
    }

    port_ = iport;
    disable();

    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET)
    {
        std::string error;

        #ifdef _WIN32

        error += ("Connect failed, socket error = " + std::to_string(WSAGetLastError()));

        #else

        error += ("Connect failed, socket error = " + std::to_string(errno));

        #endif

        THROWM(fplog::exceptions::Connect_Failed, error.c_str());
    }

    sockaddr_in listen_addr;
    listen_addr.sin_family=AF_INET;
    listen_addr.sin_port=htons(iport);

    if (localhost_)
        listen_addr.sin_addr.s_addr = 0x0100007f;
    else
        listen_addr.sin_addr.s_addr = 0;

    // Set the exclusive address option
    int opt_val = 1;

    #ifdef _WIN32

    setsockopt(socket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *) &opt_val, sizeof(opt_val));

    #else

    opt_val = 0;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));
    setsockopt(socket_, SOL_SOCKET, SO_REUSEPORT, &opt_val, sizeof(opt_val));

    #endif

    if (0 != bind(socket_, (sockaddr*)&listen_addr, sizeof(listen_addr)))
    {
        if (!localhost_)
        {
            shutdown(socket_, SD_BOTH);
            closesocket(socket_);
            THROW(fplog::exceptions::Connect_Failed);
        }

        THROW(fplog::exceptions::Connect_Failed);
    }

    //Setting bigger socket receive and send buffers
    int sock_buf_sz =  256 * 1024;
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &sock_buf_sz, sizeof(sock_buf_sz)) != 0)
    {
        closesocket(socket_);
        THROW(fplog::exceptions::Connect_Failed);
    }

    sock_buf_sz =  256 * 1024;
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, &sock_buf_sz, sizeof(sock_buf_sz)) != 0)
    {
        closesocket(socket_);
        THROW(fplog::exceptions::Connect_Failed);
    }

    enabled_ = true;
}

void Udp_Transport::disable()
{
    std::lock_guard<std::recursive_mutex> lock_read(read_mutex_);
    std::lock_guard<std::recursive_mutex> lock_write(write_mutex_);

    if (!enabled_)
        return;

    int res = 0;
    if (SOCKET_ERROR == shutdown(socket_, SD_BOTH))
    {
        #ifdef _WIN32

        res = WSAGetLastError();

        #else

        res = errno;

        #endif

        //TODO: do something meaningful with the error
    }

    if (SOCKET_ERROR == closesocket(socket_))
    {
        #ifdef _WIN32

        res = WSAGetLastError();

        #else

        res = errno;

        #endif

        //TODO: do something meaningful with the error
    }

    enabled_ = false;
}

size_t Udp_Transport::read(void* buf, size_t buf_size, Address& user_data, size_t timeout)
{
    std::lock_guard<std::recursive_mutex> lock(read_mutex_);

    if (!enabled_)
        THROW(fplog::exceptions::Read_Failed);

    static unsigned short buffered_port = 0;
    static unsigned int buffered_ip = 0;

buffered_read:

    if (bytes_in_buffer_ > 0)
    {
        int min_len = static_cast<int>(buf_size);
        if (min_len > bytes_in_buffer_)
            min_len = bytes_in_buffer_;

        memcpy(buf, read_buffer_ + read_position_, static_cast<size_t>(min_len));

        bytes_in_buffer_ -= min_len;
        read_position_ += min_len;

        user_data.ip = buffered_ip;
        user_data.port = buffered_port;

        if (chaos_rate_ != 0)
        {
            if (chaos_counter_ == chaos_rate_)
            {
                debug_logging::g_logger.log("chaos inserted!\n");
                std::uniform_int_distribution<int> pos_range(0, min_len);
                reinterpret_cast<char*>(buf)[pos_range(chaos_gen_)] = 'F';

                chaos_counter_ = 1;
            }
            else
                chaos_counter_++;
        }

        return static_cast<size_t>(min_len);
    }
    else
    {
        bytes_in_buffer_ = 0;
        read_position_ = 0;
    }

    sockaddr_in remote_addr;
    int addr_len = sizeof(remote_addr);

    fd_set fdset;

#ifdef _WIN32

    fdset.fd_count = 1;
    fdset.fd_array[0] = socket_;

#else

    FD_ZERO(&fdset);
    FD_SET(socket_, &fdset);

#endif

    timeval to;
    to.tv_sec = static_cast<long>(timeout / 1000);
    to.tv_usec = static_cast<int>((timeout % 1000) * 1000);

    int res = select(static_cast<int>(socket_ + 1), &fdset, nullptr, nullptr, &to);
    if (res == 0)
        THROW(fplog::exceptions::Timeout);
    if (res != 1)
        THROW(fplog::exceptions::Read_Failed);

    bool buffered = true;

    char* read_buf = read_buffer_;
    int read_buf_size = read_buffer_size_;

    if (buf_size >= static_cast<unsigned>(read_buffer_size_))
    {
        read_buf = reinterpret_cast<char*>(buf);
        read_buf_size = static_cast<int>(buf_size);
        buffered = false;
    }

#ifdef __APPLE__
    res = recvfrom((int)socket_, read_buf, read_buf_size, 0, (sockaddr*)&remote_addr, (socklen_t *)&addr_len);
#else
    res = recvfrom(socket_, read_buf, read_buf_size, 0, (sockaddr*)&remote_addr, (socklen_t *)&addr_len);
#endif

    if (res != SOCKET_ERROR)
    {
        buffered_port = ntohs(remote_addr.sin_port);
        buffered_ip = remote_addr.sin_addr.s_addr;

        if (!null_data(user_data))
        {
            user_data.ip = buffered_ip;
            user_data.port = buffered_port;
        }

        if (buffered)
        {
            bytes_in_buffer_ = res;
            goto buffered_read;
        }

        if (chaos_rate_ != 0)
        {
            if (chaos_counter_ == chaos_rate_)
            {
                debug_logging::g_logger.log("chaos inserted!\n");
                std::uniform_int_distribution<int> pos_range(0, res);
                reinterpret_cast<char*>(buf)[pos_range(chaos_gen_)] = 'F';

                chaos_counter_ = 1;
            }
            else
                chaos_counter_++;
        }

        return static_cast<size_t>(res);
    }
    else
        THROW(fplog::exceptions::Read_Failed);
}

size_t Udp_Transport::write(const void* buf, size_t buf_size, Address& user_data, size_t timeout)
{
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);

    if (!enabled_ || null_data(user_data))
        THROW(fplog::exceptions::Write_Failed);

    sockaddr_in remote_addr;
    int addr_len = sizeof(remote_addr);
/*
    if (localhost_)
    {
        ((char*)&(remote_addr.sin_addr.s_addr))[0] = 127;
        ((char*)&(remote_addr.sin_addr.s_addr))[1] = 0;
        ((char*)&(remote_addr.sin_addr.s_addr))[2] = 0;
        ((char*)&(remote_addr.sin_addr.s_addr))[3] = 1;
    }
    else
    {
        ((char*)&(remote_addr.sin_addr.s_addr))[0] = ip_[0];
        ((char*)&(remote_addr.sin_addr.s_addr))[1] = ip_[1];
        ((char*)&(remote_addr.sin_addr.s_addr))[2] = ip_[2];
        ((char*)&(remote_addr.sin_addr.s_addr))[3] = ip_[3];
    }
*/
    Ip_Address remote;
    unsigned short port;

    remote.ip = user_data.ip;
    port = htons(user_data.port);

    memset(&remote_addr, 0, sizeof(remote_addr));

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = remote.ip;
    remote_addr.sin_port = port;


    fd_set fdset;

#ifdef _WIN32

    fdset.fd_count = 1;
    fdset.fd_array[0] = socket_;

#else

    FD_ZERO(&fdset);
    FD_SET(socket_, &fdset);

#endif


    timeval to;
    to.tv_sec = static_cast<long>(timeout / 1000);
    to.tv_usec = static_cast<int>((timeout % 1000) * 1000);

    int res = select(static_cast<int>(socket_ + 1), nullptr, &fdset, nullptr, &to);
    if (res == 0)
        THROW(fplog::exceptions::Timeout);
    if (res != 1)
        THROW(fplog::exceptions::Write_Failed);

    res = sendto(socket_, buf, static_cast<size_t>(buf_size), 0, (sockaddr*)&remote_addr, static_cast<unsigned int>(addr_len));
    if (res != SOCKET_ERROR)
        return static_cast<size_t>(res);
    else
    {
        #ifndef _WIN32
            int e = errno;
            std::string msg("Socket error: " + std::to_string(e));
            THROWM(fplog::exceptions::Write_Failed, msg.c_str())
        #else
            THROW(fplog::exceptions::Write_Failed);
        #endif
    }
}

Udp_Transport::Udp_Transport():
enabled_(false),
chaos_gen_(31337)
{
    bytes_in_buffer_ = 0;
    read_buffer_ = new char[read_buffer_size_];
}

Udp_Transport::~Udp_Transport()
{
    std::lock_guard<std::recursive_mutex> read_lock(read_mutex_);
    std::lock_guard<std::recursive_mutex> write_lock(write_mutex_);

    disable();

    bytes_in_buffer_ = 0;
    delete [] read_buffer_;
    read_buffer_ = nullptr;
}

};
