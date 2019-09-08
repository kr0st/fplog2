#include <piped_sequence.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <thread>

unsigned long long read_sequence_number(size_t timeout)
{
    unsigned long long sequence = 0, reversed_sequence = 0;

    auto timer_start_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    auto timer_stop_ms = timer_start_ms;

    std::chrono::milliseconds timeout_ms(timeout);

    int pipe = -1;

    do
    {
        pipe = open(sequence_pipe_name, O_RDONLY);

        if (pipe != -1)
            break;
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        timer_stop_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    } while (timer_stop_ms - timer_start_ms < timeout_ms);

    if (pipe == -1)
        return 0;

    do
    {
        read(pipe, &reversed_sequence, sizeof(sequence));

        char* p_seq = reinterpret_cast<char*>(&sequence);
        char* p_rev_seq = reinterpret_cast<char*>(&reversed_sequence);

        for (size_t i = 0; i < sizeof(sequence); ++i)
            p_seq[i] = p_rev_seq[sizeof(sequence) - 1 - i];

        if (sequence != 0)
            break;
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        timer_stop_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    } while (timer_stop_ms - timer_start_ms < timeout_ms);

    close(pipe);

    return sequence;
}
