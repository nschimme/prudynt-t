#include "JPEGWorker.hpp"

#include "Config.hpp"
#include "Logger.hpp"
#include "WorkerUtils.hpp"
#include "globals.hpp"
#include "hal/Encoder.hpp"
#if HAL_DIR == imp
#include "hal/imp/IMPEncoderImpl.hpp"
#elif HAL_DIR == v4l
#include "hal/v4l/V4LEncoderImpl.hpp"
#endif

#include <fcntl.h>
#include <unistd.h>

#define MODULE "JPEGWorker"

JPEGWorker::JPEGWorker(int jpgChnIndex, int impEncoderChn)
    : jpgChn(jpgChnIndex)
    , impEncChn(impEncoderChn)
{
    LOG_DEBUG("JPEGWorker created for JPEG channel index " << jpgChn << " (IMP Encoder Channel "
                                                           << impEncChn << ")");
}

JPEGWorker::~JPEGWorker()
{
    LOG_DEBUG("JPEGWorker destroyed for JPEG channel index " << jpgChn);
}

int JPEGWorker::save_jpeg_stream(int fd, EncodedStream *stream)
{
    for (const auto& frame : stream->frames) {
        int ret = write(fd, frame.data.data(), frame.data.size());
        if (ret != static_cast<int>(frame.data.size())) {
            printf("Stream write error: %s\n", strerror(errno));
            return -1;
        }
    }
    return 0;
}

void JPEGWorker::run()
{
    LOG_DEBUG("Start JPEG processing run loop for index " << jpgChn);
    Encoder* encoder = global_jpeg[jpgChn]->encoder;
    int targetFps = global_jpeg[jpgChn]->stream->jpeg_idle_fps;

    while (global_jpeg[jpgChn]->running)
    {
        bool request_or_overrun = global_jpeg[jpgChn]->request_or_overrun();

        if (request_or_overrun || targetFps)
        {
            if (encoder->poll_stream(cfg->general.imp_polling_timeout) == 0)
            {
                EncodedStream stream = encoder->get_stream();
                if (!stream.frames.empty())
                {
                    const char *tempPath = "/tmp/snapshot.tmp";
                    const char *finalPath = global_jpeg[jpgChn]->stream->jpeg_path;
                    int snap_fd = open(tempPath, O_RDWR | O_CREAT | O_TRUNC, 0666);
                    if (snap_fd >= 0)
                    {
                        save_jpeg_stream(snap_fd, &stream);
                        close(snap_fd);
                        if (rename(tempPath, finalPath) != 0) {
                            LOG_ERROR("Failed to move JPEG snapshot from " << tempPath << " to " << finalPath);
                            std::remove(tempPath);
                        }
                    } else {
                        LOG_ERROR("Failed to open JPEG snapshot for writing: " << tempPath);
                    }
                }
                encoder->release_stream();
            }
            global_jpeg[jpgChn]->last_image = steady_clock::now();
        } else {
            usleep(10000); // Sleep when idle
        }
    }
}

void *JPEGWorker::thread_entry(void *arg)
{
    LOG_DEBUG("Start jpeg_grabber thread.");

    StartHelper *sh = static_cast<StartHelper *>(arg);
    int jpgChn = sh->encChn - 2;

    global_jpeg[jpgChn]->streamChn = global_jpeg[jpgChn]->stream->jpeg_channel;

    if (global_jpeg[jpgChn]->streamChn == 0) {
        cfg->stream2.width = cfg->stream0.width;
        cfg->stream2.height = cfg->stream0.height;
    } else {
        cfg->stream2.width = cfg->stream1.width;
        cfg->stream2.height = cfg->stream1.height;
    }

#if HAL_DIR == imp
    global_jpeg[jpgChn]->encoder = new IMPEncoderImpl(global_jpeg[jpgChn]->stream, sh->encChn, global_jpeg[jpgChn]->streamChn, "stream2");
#elif HAL_DIR == v4l
    global_jpeg[jpgChn]->encoder = new V4LEncoderImpl();
#endif
    global_jpeg[jpgChn]->encoder->init();

    sh->has_started.release();

    if (!global_jpeg[jpgChn]->encoder->start()) {
        return 0;
    }

    global_jpeg[jpgChn]->active = true;
    global_jpeg[jpgChn]->running = true;
    JPEGWorker worker(jpgChn, sh->encChn);
    worker.run();

    if (global_jpeg[jpgChn]->encoder)
    {
        global_jpeg[jpgChn]->encoder->stop();
        global_jpeg[jpgChn]->encoder->deinit();
        delete global_jpeg[jpgChn]->encoder;
        global_jpeg[jpgChn]->encoder = nullptr;
    }

    return 0;
}
