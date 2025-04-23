#ifndef VIDEO_WORKER_HPP
#define VIDEO_WORKER_HPP

#include "IMPEncoder.hpp"

class VideoWorker
{
public:
    explicit VideoWorker(int encChn, int jpgChn);
    ~VideoWorker();

    static void *thread_entry(void *arg);

private:
    void run();

    void handleVideoStream(IMPEncoderStream &stream);
    void handleJpegStream();
    void updateStats(uint32_t &fps, uint32_t &bps, unsigned long long &ms);
    void handleIdleState();

    int encChn;
    int jpgChn;
};

#endif // VIDEO_PROCESSOR_HPP
