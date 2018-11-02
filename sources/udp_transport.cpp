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

void Udp_Transport::enable(const Session_Manager::Params& params)
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
    {
        ((char*)&(listen_addr.sin_addr.s_addr))[0] = 127;
        ((char*)&(listen_addr.sin_addr.s_addr))[1] = 0;
        ((char*)&(listen_addr.sin_addr.s_addr))[2] = 0;
        ((char*)&(listen_addr.sin_addr.s_addr))[3] = 1;
    }
    else
    {
        ((char*)&(listen_addr.sin_addr.s_addr))[0] = 0;
        ((char*)&(listen_addr.sin_addr.s_addr))[1] = 0;
        ((char*)&(listen_addr.sin_addr.s_addr))[2] = 0;
        ((char*)&(listen_addr.sin_addr.s_addr))[3] = 0;
    }

    // Set the exclusive address option
    int opt_val = 1;

    #ifdef _WIN32

    setsockopt(socket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *) &opt_val, sizeof(opt_val));

    #else

    opt_val = 0;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, (char *) &opt_val, sizeof(opt_val));
    setsockopt(socket_, SOL_SOCKET, SO_REUSEPORT, (char *) &opt_val, sizeof(opt_val));

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

size_t Udp_Transport::read(void* buf, size_t buf_size, size_t timeout, Extended_Data& user_data)
{
    std::lock_guard<std::recursive_mutex> lock(read_mutex_);

    if (!enabled_)
        THROW(fplog::exceptions::Read_Failed);

    time_point<system_clock, system_clock::duration> timer_start(system_clock::now());
    auto check_time_out = [&timeout, &timer_start]()
    {
        auto timer_start_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(timer_start);
        auto timer_stop_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        std::chrono::milliseconds timeout_ms(timeout);

        if (timer_stop_ms - timer_start_ms >= timeout_ms)
            THROW(fplog::exceptions::Timeout);
    };

retry:

    check_time_out();

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
    to.tv_usec = static_cast<long>((timeout % 1000) * 1000);

    int res = select(static_cast<int>(socket_ + 1), &fdset, 0, 0, &to);
    if (res == 0)
        THROW(fplog::exceptions::Timeout);
    if (res != 1)
        THROW(fplog::exceptions::Read_Failed);

#ifdef __APPLE__
    res = recvfrom((int)socket_, buf, buf_size, 0, (sockaddr*)&remote_addr, (socklen_t *)&addr_len);
#else
    res = recvfrom(socket_, (char*)buf, static_cast<int>(buf_size), 0, (sockaddr*)&remote_addr, &addr_len);
#endif

    if (res != SOCKET_ERROR)
    {
        if (!null_data(user_data))
        {
            user_data.clear();

            user_data.push_back(remote_addr.sin_addr.s_addr);
            user_data.push_back(ntohs(remote_addr.sin_port));
        }

        return res;
    }
    else
        THROW(fplog::exceptions::Read_Failed);

    return 0;
}

size_t Udp_Transport::write(const void* buf, size_t buf_size, size_t timeout, Extended_Data& user_data)
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
    std::experimental::any_cast<unsigned int>(user_data[0]);

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = (u_short)(high_uid_ ? uid_.low : uid_.high);

    if (!localhost_)
        remote_addr.sin_port = (u_short)uid_.high;

    remote_addr.sin_port = htons(remote_addr.sin_port);

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
    to.tv_usec = static_cast<long>((timeout % 1000) * 1000);

    int res = select(static_cast<int>(socket_ + 1), 0, &fdset, 0, &to);
    if (res == 0)
        THROW(fplog::exceptions::Timeout);
    if (res != 1)
        THROW(fplog::exceptions::Write_Failed);

    res = sendto(socket_, (char*)buf, static_cast<int>(buf_size), 0, (sockaddr*)&remote_addr, addr_len);
    if (res != SOCKET_ERROR)
        return res;
    else
    {
        int e = errno;
        THROW(fplog::exceptions::Write_Failed);
    }

    return 0;
}

Udp_Transport::Udp_Transport():
enabled_(false)
{
}

Udp_Transport::~Udp_Transport()
{
    std::lock_guard<std::recursive_mutex> read_lock(read_mutex_);
    std::lock_guard<std::recursive_mutex> write_lock(write_mutex_);

    disable();
}

};
