#include <sprot.h>

namespace sprot {
namespace implementation {

Options::Options()
{
    max_frame_size = 4096; //maximum size of a single protocol frame
    mtu = max_frame_size - sizeof(Frame::bytes); //the largest payload that could be transfered in a single frame
    no_ack_count = 5; //how many DATA frames could be sent before ACK is requested
    storage_max = 100;//how many received frames could be temporarily stored
                      //to ensure error correction (retransmit) will work if needed
    storage_trim = 50;//how many frames to delete from temporary storage if storage_max is reached
    op_timeout = 500; //a single operation timeout, should be considerably less than the whole read/write user timeout
    max_retries = 20; //maximum number of retries of the unsuccessful operation
    max_connections = 1024;    //maximum number of pending and established connections
    max_requests_in_queue = 21;//maximum number of pending requests per established or pending connection
}

void Options::Load(Params params)
{
    if (params.find("max_frame_size") != params.end())
    {
        long max_frame = static_cast<long int>(std::stoi(params.find("max_frame_size")->second));
        if ((max_frame < 128) || (max_frame > 10 * 1024))
            max_frame = 4096;

        max_frame_size = static_cast<unsigned int>(max_frame);
        mtu = max_frame_size - sizeof(Frame::bytes);
    }

    if (params.find("no_ack_count") != params.end())
    {
        long no_ack = static_cast<long int>(std::stoi(params.find("no_ack_count")->second));
        if ((no_ack >= 1) && (no_ack < 100))
            no_ack_count = static_cast<unsigned int>(no_ack);
    }

    if (params.find("storage_max") != params.end())
    {
        long stor_max = static_cast<long int>(std::stoi(params.find("storage_max")->second));
        if ((stor_max >= 21) && (stor_max < 1000))
        {
            storage_max = static_cast<unsigned int>(stor_max);
            storage_trim = storage_max / 2;
        }
    }

    if (params.find("storage_trim") != params.end())
    {
        long stor_trim = static_cast<long int>(std::stoi(params.find("storage_trim")->second));
        if ((stor_trim >= 21) && (stor_trim < 1000) && (stor_trim <= storage_max))
            storage_trim = static_cast<unsigned int>(stor_trim);
    }

    if (params.find("op_timeout") != params.end())
    {
        long timeout = static_cast<long int>(std::stoi(params.find("op_timeout")->second));
        if ((timeout >= 50) && (timeout < 5000))
            op_timeout = static_cast<unsigned int>(timeout);
    }

    if (params.find("max_retries") != params.end())
    {
        long retries = static_cast<long int>(std::stoi(params.find("max_retries")->second));
        if ((retries >= 1) && (retries < 100))
            max_retries = static_cast<unsigned int>(retries);
    }
}

Options options;

}}
