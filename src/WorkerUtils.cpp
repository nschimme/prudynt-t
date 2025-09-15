#include "WorkerUtils.hpp"

#include <cstddef>
#include <time.h>

namespace WorkerUtils {

void getMonotonicTimeOfDay(struct timeval *tv) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;
}


unsigned long long getMonotonicTimeDiffInMs(struct timeval *startTime)
{
    struct timeval currentTime;
    getMonotonicTimeOfDay(&currentTime);

    long seconds = currentTime.tv_sec - startTime->tv_sec;
    long microseconds = currentTime.tv_usec - startTime->tv_usec;

    unsigned long long milliseconds = (seconds * 1000) + (microseconds / 1000);

    return milliseconds;
}

} // namespace WorkerUtils
