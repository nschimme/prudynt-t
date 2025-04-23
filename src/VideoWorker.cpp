#include "VideoWorker.hpp"

#include "Config.hpp"
#include "IMPEncoder.hpp"
#include "IMPFramesource.hpp"
#include "Logger.hpp"
#include "WorkerUtils.hpp"
#include "globals.hpp"

#define MODULE "VideoWorker"

VideoWorker::VideoWorker(int chn, int jpg_chn)
    : encChn(chn)
    , jpgChn(jpg_chn)
{
    LOG_DEBUG("VideoWorker created for channel " << encChn << " (JPEG channel " << jpgChn << ")");
}

VideoWorker::~VideoWorker()
{
    LOG_DEBUG("VideoWorker destroyed for channel " << encChn);
}

void VideoWorker::handleVideoStream(IMPEncoderStream &stream)
{
    for (uint32_t i = 0; i < stream.packCount; ++i)
    {
        if (global_video[encChn]->hasDataCallback)
        {
#if defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
            uint8_t *start = (uint8_t *) stream.virAddr + stream.pack[i].offset;
            uint8_t *end = start + stream.pack[i].length;
#elif defined(PLATFORM_T10) || defined(PLATFORM_T20) || defined(PLATFORM_T21) \
    || defined(PLATFORM_T23) || defined(PLATFORM_T30)
            uint8_t *start = (uint8_t *) stream.pack[i].virAddr;
            uint8_t *end = (uint8_t *) stream.pack[i].virAddr + stream.pack[i].length;
#endif
            H264NALUnit nalu;

            /* timestamp fix, can be removed if solved
            nalu.imp_ts = stream.pack[i].timestamp;
            nalu.time = encoder_time;
            */

            // We use start+4 because the encoder inserts 4-byte MPEG
            //'startcodes' at the beginning of each NAL. Live555 complains
            nalu.data.insert(nalu.data.end(), start + 4, end);
            if (global_video[encChn]->idr == false)
            {
#if defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
                if (stream.pack[i].nalType.h264NalType == 7
                    || stream.pack[i].nalType.h264NalType == 8
                    || stream.pack[i].nalType.h264NalType == 5)
                {
                    global_video[encChn]->idr = true;
                }
                else if (stream.pack[i].nalType.h265NalType == 32)
                {
                    global_video[encChn]->idr = true;
                }
#elif defined(PLATFORM_T10) || defined(PLATFORM_T20) || defined(PLATFORM_T21) \
    || defined(PLATFORM_T23)
                if (stream.pack[i].dataType.h264Type == 7 || stream.pack[i].dataType.h264Type == 8
                    || stream.pack[i].dataType.h264Type == 5)
                {
                    global_video[encChn]->idr = true;
                }
#elif defined(PLATFORM_T30)
                if (stream.pack[i].dataType.h264Type == 7 || stream.pack[i].dataType.h264Type == 8
                    || stream.pack[i].dataType.h264Type == 5)
                {
                    global_video[encChn]->idr = true;
                }
                else if (stream.pack[i].dataType.h265Type == 32)
                {
                    global_video[encChn]->idr = true;
                }
#endif
            }

            if (global_video[encChn]->idr == true)
            {
                if (!global_video[encChn]->msgChannel->write(nalu))
                {
                    LOG_ERROR("video " << "channel:" << encChn << ", "
                                       << "package:" << i << " of " << stream.packCount << ", "
                                       << "packageSize:" << nalu.data.size()
                                       << ".  !sink clogged!");
                }
                else
                {
                    std::unique_lock<std::mutex> lock_stream{
                        global_video[encChn]->onDataCallbackLock};
                    if (global_video[encChn]->onDataCallback)
                        global_video[encChn]->onDataCallback();
                }
            }
#if defined(USE_AUDIO_STREAM_REPLICATOR)
            /* Since the audio stream is permanently in use by the stream replicator, 
             * and the audio grabber and encoder standby is also controlled by the video threads
             * we need to wakeup the audio thread 
            */
            if (cfg->audio.input_enabled && !global_audio[0]->active && !global_restart)
            {
                LOG_DDEBUG("NOTIFY AUDIO " << !global_audio[0]->active << " "
                                           << cfg->audio.input_enabled);
                global_audio[0]->should_grab_frames.notify_one();
            }
#endif
        }
    }
}

void VideoWorker::handleJpegStream()
{
    // Check if this worker is responsible for the specified JPEG channel
    if (jpgChn < 0 || global_jpeg[jpgChn]->streamChn != encChn)
    {
        return; // Not responsible for this JPEG channel
    }

    auto now = steady_clock::now();
    bool request_or_overrun = global_jpeg[jpgChn]->request_or_overrun();
    int targetFps = request_or_overrun ? global_jpeg[jpgChn]->stream->fps
                                       : global_jpeg[jpgChn]->stream->jpeg_idle_fps;

    if (targetFps > 0)
    {
        auto diff_last_image = duration_cast<milliseconds>(now - global_jpeg[jpgChn]->last_image)
                                   .count();

        // Check if it's time to produce a frame based on target FPS
        if (diff_last_image >= ((1000 / targetFps) - targetFps / 10))
        {
            if (IMP_Encoder_PollingStream(global_jpeg[jpgChn]->encChn,
                                          cfg->general.imp_polling_timeout)
                == 0)
            {
                IMPEncoderStream stream;
                if (IMP_Encoder_GetStream(global_jpeg[jpgChn]->encChn, &stream, GET_STREAM_BLOCKING)
                    == 0)
                {
                    JPEGFrame frame;
                    size_t total_size = 0;

                    // Calculate total size first
                    for (uint32_t i = 0; i < stream.packCount; ++i)
                    {
                        total_size += stream.pack[i].length;
                    }
                    frame.data.reserve(total_size); // Reserve space

                    // Copy data from IMP stream packs to JPEGFrame vector
                    for (uint32_t i = 0; i < stream.packCount; ++i)
                    {
                        void *data_ptr;
                        size_t data_len;
#if defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
                        IMPEncoderPack *pack = &stream.pack[i];
                        uint32_t remSize = 0;
                        if (pack->length)
                        {
                            remSize = stream.streamSize - pack->offset;
                            data_ptr = (void *) ((char *) stream.virAddr
                                                 + ((remSize < pack->length) ? 0 : pack->offset));
                            data_len = (remSize < pack->length) ? remSize : pack->length;
                        }
                        else
                        {
                            continue; // Skip empty packs
                        }
#elif defined(PLATFORM_T10) || defined(PLATFORM_T20) || defined(PLATFORM_T21) \
    || defined(PLATFORM_T23) || defined(PLATFORM_T30)
                        data_ptr = reinterpret_cast<void *>(stream.pack[i].virAddr);
                        data_len = stream.pack[i].length;
#endif
                        frame.data.insert(frame.data.end(),
                                          static_cast<uint8_t *>(data_ptr),
                                          static_cast<uint8_t *>(data_ptr) + data_len);

#if defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
                        if (remSize && pack->length > remSize)
                        {
                            data_ptr = (void *) ((char *) stream.virAddr);
                            data_len = pack->length - remSize;
                            frame.data.insert(frame.data.end(),
                                              static_cast<uint8_t *>(data_ptr),
                                              static_cast<uint8_t *>(data_ptr) + data_len);
                        }
#endif
                    }

                    // Write the complete frame to the message channel
                    if (!global_jpeg[jpgChn]->msgChannel->write(frame))
                    {
                        LOG_DDEBUG("JPEG channel " << jpgChn << " clogged, frame dropped.");
                    }

                    IMP_Encoder_ReleaseStream(global_jpeg[jpgChn]->encChn, &stream);
                    global_jpeg[jpgChn]->last_image = steady_clock::now();
                }
                else
                {
                    LOG_ERROR("IMP_Encoder_GetStream(" << global_jpeg[jpgChn]->encChn
                                                       << ") failed for JPEG");
                }
            }
            else
            {
                LOG_DDEBUG("IMP_Encoder_PollingStream(" << global_jpeg[jpgChn]->encChn
                                                        << ") timeout for JPEG");
            }
        }
    }
}

void VideoWorker::updateStats(uint32_t &fps, uint32_t &bps, unsigned long long &ms)
{
    ms = WorkerUtils::tDiffInMs(&global_video[encChn]->stream->stats.ts);
    if (ms > 1000)
    {
        /* currently we write into osd and stream stats,
        * osd will be removed and redesigned in future
        */
        global_video[encChn]->stream->stats.bps = bps;
        global_video[encChn]->stream->osd.stats.bps = bps;
        global_video[encChn]->stream->stats.fps = fps;
        global_video[encChn]->stream->osd.stats.fps = fps;

        fps = 0;
        bps = 0;
        gettimeofday(&global_video[encChn]->stream->stats.ts, NULL);
        global_video[encChn]->stream->osd.stats.ts = global_video[encChn]->stream->stats.ts;

        /*
        IMPEncoderCHNStat encChnStats;
        IMP_Encoder_Query(channel->encChn, &encChnStats);
        LOG_DEBUG("ChannelStats::" << channel->encChn <<
                    ", registered:" << encChnStats.registered <<
                    ", leftPics:" << encChnStats.leftPics <<
                    ", leftStreamBytes:" << encChnStats.leftStreamBytes <<
                    ", leftStreamFrames:" << encChnStats.leftStreamFrames <<
                    ", curPacks:" << encChnStats.curPacks <<
                    ", work_done:" << encChnStats.work_done);
        */
        if (global_video[encChn]->idr_fix)
        {
            IMP_Encoder_RequestIDR(encChn);
            global_video[encChn]->idr_fix--;
        }
    }
}

void VideoWorker::handleIdleState()
{
    LOG_DDEBUG("VIDEO LOCK" << " channel:" << encChn << " hasCallbackIsNull:"
                            << (global_video[encChn]->onDataCallback == nullptr)
                            << " restartVideo:" << global_restart_video
                            << " runForJpeg:" << global_video[encChn]->run_for_jpeg);

    global_video[encChn]->stream->stats.bps = 0;
    global_video[encChn]->stream->stats.fps = 0;
    global_video[encChn]->stream->osd.stats.bps = 0;
    global_video[encChn]->stream->osd.stats.fps = 0;

    std::unique_lock<std::mutex> lock_stream{mutex_main};
    global_video[encChn]->active = false;
    while (global_video[encChn]->onDataCallback == nullptr && !global_restart_video
           && !global_video[encChn]->run_for_jpeg)
        global_video[encChn]->should_grab_frames.wait(lock_stream);

    global_video[encChn]->active = true;
    global_video[encChn]->is_activated.release();

    // unlock audio
    global_audio[0]->should_grab_frames.notify_one();

    LOG_DDEBUG("VIDEO UNLOCK" << " channel:" << encChn);
}

void VideoWorker::run()
{
    LOG_DEBUG("Start video processing run loop for stream " << encChn);

    uint32_t bps = 0;
    uint32_t fps = 0;
    uint32_t error_count = 0; // Keep track of polling errors
    unsigned long long ms = 0;

    while (global_video[encChn]->running)
    {
        // Check if we need to be active (video callback or JPEG request)
        bool need_active = global_video[encChn]->hasDataCallback
                           || (jpgChn >= 0 && global_video[encChn]->run_for_jpeg);

        if (need_active)
        {
            // Handle JPEG stream production if responsible
            handleJpegStream();

            // Handle H.264/H.265 video stream production
            if (global_video[encChn]->hasDataCallback)
            {
                if (IMP_Encoder_PollingStream(encChn, cfg->general.imp_polling_timeout) == 0)
                {
                    IMPEncoderStream stream;
                    if (IMP_Encoder_GetStream(encChn, &stream, GET_STREAM_BLOCKING) == 0)
                    {
                        // Calculate BPS/FPS for video stream
                        for (uint32_t i = 0; i < stream.packCount; ++i)
                        {
                            fps++;
                            bps += stream.pack[i].length;
                        }

                        handleVideoStream(stream); // Process the video NALUs
                        IMP_Encoder_ReleaseStream(encChn, &stream);
                        error_count = 0; // Reset error count on success
                    }
                    else
                    {
                        LOG_ERROR("IMP_Encoder_GetStream(" << encChn << ") failed");
                        error_count++;
                    }
                }
                else
                {
                    error_count++;
                    LOG_DDEBUG("IMP_Encoder_PollingStream(" << encChn << ", "
                                                            << cfg->general.imp_polling_timeout
                                                            << ") timeout !");
                }
            }
            else
            {
                // If only running for JPEG, add a small sleep to avoid busy-waiting
                usleep(1000);
            }

            // Update stats periodically
            updateStats(fps, bps, ms);
        }
        else if (!global_restart_video) // Go idle if no callbacks and no restart pending
        {
            handleIdleState();
        }
        else
        {
            // If restart is pending but not active, sleep briefly
            usleep(10000);
        }
    }
    LOG_DEBUG("Exiting video processing run loop for stream " << encChn);
}

void *VideoWorker::thread_entry(void *arg)
{
    StartHelper *sh = static_cast<StartHelper *>(arg);
    int encChn = sh->encChn;

    LOG_DEBUG("Start stream_grabber thread for stream " << encChn);

    int ret;

    // Initialize video resources
    global_video[encChn]->imp_framesource = IMPFramesource::createNew(global_video[encChn]->stream,
                                                                      &cfg->sensor,
                                                                      encChn);
    global_video[encChn]->imp_encoder = IMPEncoder::createNew(global_video[encChn]->stream,
                                                              encChn,
                                                              encChn,
                                                              global_video[encChn]->name);
    global_video[encChn]->imp_framesource->enable();
    global_video[encChn]->run_for_jpeg = false;

    // Start receiving video pictures
    ret = IMP_Encoder_StartRecvPic(encChn);
    LOG_DEBUG_OR_ERROR(ret, "IMP_Encoder_StartRecvPic(" << encChn << ")");
    if (ret != 0)
        return nullptr;

    // Initialize JPEG resources if responsible
    int jpgChn;
    if (cfg->stream2.enabled && global_jpeg[0]->streamChn == encChn)
    {
        jpgChn = 0;
    }
    else
    {
        jpgChn = -1;
    }
    if (jpgChn != -1)
    {
        LOG_DEBUG("Initializing JPEG encoder for channel " << jpgChn << " on video worker "
                                                           << encChn);
        global_jpeg[jpgChn]->imp_encoder
            = IMPEncoder::createNew(global_jpeg[jpgChn]->stream,
                                    global_jpeg[jpgChn]->encChn,
                                    global_jpeg[jpgChn]->streamChn,
                                    "stream2"); // Assuming JPEG is stream2
        ret = IMP_Encoder_StartRecvPic(global_jpeg[jpgChn]->encChn);
        LOG_DEBUG_OR_ERROR(ret,
                           "IMP_Encoder_StartRecvPic(" << global_jpeg[jpgChn]->encChn
                                                       << ") for JPEG");
        if (ret != 0)
            return nullptr;
    }

    // inform main that initialization is complete
    sh->has_started.release();

    /* 'active' indicates, the thread is activly polling and grabbing images
     * 'running' describes the runlevel of the thread, if this value is set to false
     *           the thread exits and cleanup all ressources 
     */
    global_video[encChn]->active = true;
    global_video[encChn]->running = true;
    VideoWorker worker(encChn, jpgChn);
    worker.run();

    // Teardown JPEG resources if responsible
    if (jpgChn != -1 && global_jpeg[jpgChn]->imp_encoder)
    {
        LOG_DEBUG("Tearing down JPEG encoder for channel " << jpgChn << " on video worker "
                                                           << encChn);
        ret = IMP_Encoder_StopRecvPic(global_jpeg[jpgChn]->encChn);
        LOG_DEBUG_OR_ERROR(ret,
                           "IMP_Encoder_StopRecvPic(" << global_jpeg[jpgChn]->encChn
                                                      << ") for JPEG");
        global_jpeg[jpgChn]->imp_encoder->deinit();
        delete global_jpeg[jpgChn]->imp_encoder;
        global_jpeg[jpgChn]->imp_encoder = nullptr;
    }

    // Stop receiving video pictures
    ret = IMP_Encoder_StopRecvPic(encChn);
    LOG_DEBUG_OR_ERROR(ret, "IMP_Encoder_StopRecvPic(" << encChn << ")");    

    // Teardown video resources
    if (global_video[encChn]->imp_framesource)
    {
        global_video[encChn]->imp_framesource->disable();
        delete global_video[encChn]->imp_framesource;
        global_video[encChn]->imp_framesource = nullptr;
    }

    if (global_video[encChn]->imp_encoder)
    {
        global_video[encChn]->imp_encoder->destroy();
        global_video[encChn]->imp_encoder->deinit();
        delete global_video[encChn]->imp_encoder;
        global_video[encChn]->imp_encoder = nullptr;
    }

    LOG_DEBUG("Finished teardown for video worker " << encChn);
    return nullptr;
}
