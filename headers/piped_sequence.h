#ifndef PIPED_SEQUENCE_H
#define PIPED_SEQUENCE_H

#include <ctype.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <fcntl.h>
#include <time.h>
#include <sem_timedwait.h>

namespace sequence_number {

class Sem_Lock
{
    public:

        Sem_Lock(sem_t *semaphore): sem_(semaphore){}
        ~Sem_Lock(){ sem_post(sem_); sem_close(sem_); }

        bool try_lock(long timeout_ms)
        {
            struct timespec ts;

            clock_gettime(CLOCK_REALTIME, &ts);

            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += timeout_ms % 1000 * 1000000;

            return (sem_timedwait(sem_, &ts) == 0);
        }


    private:

        Sem_Lock();
        sem_t *sem_ = nullptr;
};

static const char* sequence_pipe_name = "/tmp/fplog2_shared_sequence";
#ifdef __arm__
static const char* sequence_sem_name = "/fp2_shm_sem";
#else
static const char* sequence_sem_name = "/tmp/fp2_shm_sem";
#endif

unsigned long long read_sequence_number(size_t timeout = 5000);

};

#endif // PIPED_SEQUENCE_H
