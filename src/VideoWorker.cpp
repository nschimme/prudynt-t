#include "VideoWorker.hpp"

#include "Config.hpp"
#include "hal/Encoder.hpp"
#if HAL_DIR == imp
#include "hal/imp/IMPEncoderImpl.hpp"
#elif HAL_DIR == v4l
#include "hal/v4l/V4LEncoderImpl.hpp"
#endif
#include "IMPFramesource.hpp"
#include "Logger.hpp"
#include "WorkerUtils.hpp"
#include "globals.hpp"

#undef MODULE
#define MODULE "VideoWorker"

VideoWorker::VideoWorker(int chn)
    : encChn(chn)
{
    LOG_DEBUG("VideoWorker created for channel " << encChn);
}

VideoWorker::~VideoWorker()
{
    LOG_DEBUG("VideoWorker destroyed for channel " << encChn);
}

void VideoWorker::run()
{
    LOG_DEBUG("Start video processing run loop for stream " << encChn);

    uint32_t bps = 0;
    uint32_t fps = 0;
    bool run_for_jpeg = false;
    Encoder* encoder = global_video[encChn]->encoder;

    while (global_video[encChn]->running)
    {
        run_for_jpeg = (encChn == global_jpeg[0]->streamChn && global_video[encChn]->run_for_jpeg);

        if (global_video[encChn]->hasDataCallback || run_for_jpeg)
        {
            if (encoder->poll_stream(cfg->general.imp_polling_timeout) == 0)
            {
                EncodedStream stream = encoder->get_stream();
                if (stream.frames.empty()) {
                    encoder->release_stream();
                    continue;
                }

                for (const auto& frame : stream.frames)
                {
                    fps++;
                    bps += frame.data.size();

                    if (global_video[encChn]->hasDataCallback)
                    {
                        H264NALUnit nalu;
                        nalu.time = frame.timestamp;
                        nalu.data = frame.data;

                        if (!global_video[encChn]->idr) {
                            global_video[encChn]->idr = frame.is_key_frame;
                        }

                        if (global_video[encChn]->idr)
                        {
                            if (!global_video[encChn]->msgChannel->write(nalu))
                            {
                                LOG_ERROR("video channel:" << encChn << " sink clogged!");
                            }
                            else
                            {
                                std::unique_lock<std::mutex> lock_stream{global_video[encChn]->onDataCallbackLock};
                                if (global_video[encChn]->onDataCallback)
                                    global_video[encChn]->onDataCallback();
                            }
                        }
                    }
                }

                encoder->release_stream();

                unsigned long long ms = WorkerUtils::getMonotonicTimeDiffInMs(&global_video[encChn]->stream->stats.ts);
                if (ms > 1000)
                {
                    global_video[encChn]->stream->stats.bps = bps;
                    global_video[encChn]->stream->osd.stats.bps = bps;
                    global_video[encChn]->stream->stats.fps = fps;
                    global_video[encChn]->stream->osd.stats.fps = fps;

                    fps = 0;
                    bps = 0;
                    WorkerUtils::getMonotonicTimeOfDay(&global_video[encChn]->stream->stats.ts);
                    global_video[encChn]->stream->osd.stats.ts = global_video[encChn]->stream->stats.ts;

                    if (global_video[encChn]->idr_fix)
                    {
                        encoder->request_idr();
                        global_video[encChn]->idr_fix--;
                    }
                }
            }
        }
        else if (global_video[encChn]->onDataCallback == nullptr && !global_restart_video && !global_video[encChn]->run_for_jpeg)
        {
            // ... (locking logic remains the same)
             std::unique_lock<std::mutex> lock_stream{mutex_main};
             global_video[encChn]->active = false;
             while (global_video[encChn]->onDataCallback == nullptr && !global_restart_video
                    && !global_video[encChn]->run_for_jpeg)
                 global_video[encChn]->should_grab_frames.wait(lock_stream);
             global_video[encChn]->active = true;
             global_video[encChn]->is_activated.release();
        }
    }
}

void *VideoWorker::thread_entry(void *arg)
{
    StartHelper *sh = static_cast<StartHelper *>(arg);
    int encChn = sh->encChn;

    LOG_DEBUG("Start stream_grabber thread for stream " << encChn);

    global_video[encChn]->imp_framesource = IMPFramesource::createNew(global_video[encChn]->stream, &cfg->sensor, encChn);

    // Create the concrete implementation and store it in the abstract pointer
#if HAL_DIR == imp
    global_video[encChn]->encoder = new IMPEncoderImpl(global_video[encChn]->stream, encChn, encChn, global_video[encChn]->name);
#elif HAL_DIR == v4l
    global_video[encChn]->encoder = new V4LEncoderImpl();
#endif
    global_video[encChn]->encoder->init();

    global_video[encChn]->imp_framesource->enable();
    global_video[encChn]->run_for_jpeg = false;

    sh->has_started.release();

    if (!global_video[encChn]->encoder->start()) {
        return 0;
    }

    global_video[encChn]->encoder->request_idr();
    global_video[encChn]->idr_fix = 2;

    global_video[encChn]->active = true;
    global_video[encChn]->running = true;
    VideoWorker worker(encChn);
    worker.run();

    global_video[encChn]->encoder->stop();

    if (global_video[encChn]->imp_framesource) {
        global_video[encChn]->imp_framesource->disable();
    }

    if (global_video[encChn]->encoder) {
        global_video[encChn]->encoder->deinit();
        delete global_video[encChn]->encoder;
        global_video[encChn]->encoder = nullptr;
    }

    return 0;
}
