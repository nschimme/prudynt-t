#ifndef JPEG_WORKER_HPP
#define JPEG_WORKER_HPP

#include "globals.hpp"

class JPEGWorker
{
public:
    explicit JPEGWorker(int jpgChnIndex);
    ~JPEGWorker();

    static void *thread_entry(void *arg);
    static void activate_producer(int jpgChn, int& first_request_delay_us);
    static void deinit(int jpgChn);

private:
    void run();
    int save_jpeg_stream(int fd, const JPEGFrame& frame);

    int jpgChn;
};

#endif // JPEG_PROCESSOR_HPP
