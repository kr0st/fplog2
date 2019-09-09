#pragma once
#ifdef __APPLE__

#include <semaphore.h>

int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout);

#endif
