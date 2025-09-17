#ifndef GLOBALS_HPP
#define GLOBALS_HPP

#include <memory>
#include <functional>
#include <atomic>
#include "liveMedia.hh"

#include "MsgChannel.hpp"
#include "hal/Audio.hpp"
#include "hal/Encoder.hpp"
#include "IMPFramesource.hpp" // This remains for now
#include "IMPBackchannel.hpp" // This remains for now

#define MSG_CHANNEL_SIZE 20
#define NUM_AUDIO_CHANNELS 1
#define NUM_VIDEO_CHANNELS 2

using namespace std::chrono;

extern std::mutex mutex_main;

// This struct is now defined in hal/Audio.hpp, so it's removed from here.

struct H264NALUnit
{
    std::vector<uint8_t> data;
    struct timeval time;
};

struct BackchannelFrame
{
    std::vector<uint8_t> payload;
    IMPBackchannelFormat format;
    unsigned int clientSessionId;
};

struct jpeg_stream
{
    int encChn;
    int streamChn;
    _stream *stream;
    std::atomic<bool> running;
    std::atomic<bool> active{false};
    pthread_t thread;
    Encoder *encoder; // Changed from IMPEncoder
    std::condition_variable should_grab_frames;
    std::binary_semaphore is_activated{0};

    steady_clock::time_point last_image;
    steady_clock::time_point last_subscriber;

    void request() {
        auto now = steady_clock::now();
        std::unique_lock lck(mutex_main);
        last_subscriber = now;
    }

    bool request_or_overrun() {
        return duration_cast<milliseconds>(steady_clock::now() - last_subscriber).count() < 1000;
    }

    jpeg_stream(int encChn, _stream *stream)
        : encChn(encChn), stream(stream), running(false), encoder(nullptr) {}
};

struct audio_stream
{
    int devId;
    int aiChn;
    int aeChn;
    bool running;
    bool active{false};
    pthread_t thread;
    Audio *audio; // Changed from IMPAudio
    std::shared_ptr<MsgChannel<AudioFrame>> msgChannel;
    std::function<void(void)> onDataCallback;
    std::atomic<bool> hasDataCallback;
    std::mutex onDataCallbackLock;
    std::condition_variable should_grab_frames;
    std::binary_semaphore is_activated{0};

    StreamReplicator *streamReplicator = nullptr;

    audio_stream(int devId, int aiChn, int aeChn)
        : devId(devId), aiChn(aiChn), aeChn(aeChn), running(false), audio(nullptr),
          msgChannel(std::make_shared<MsgChannel<AudioFrame>>(30)),
          onDataCallback{nullptr}, hasDataCallback{false} {}
};

struct video_stream
{
    int encChn;
    _stream *stream;
    const char *name;
    bool running;
    pthread_t thread;
    bool idr;
    int idr_fix;
    bool active{false};
    Encoder *encoder; // Changed from IMPEncoder
    IMPFramesource *imp_framesource; // This remains for now
    std::shared_ptr<MsgChannel<H264NALUnit>> msgChannel;
    std::function<void(void)> onDataCallback;
    bool run_for_jpeg;
    std::atomic<bool> hasDataCallback;
    std::mutex onDataCallbackLock;
    std::condition_variable should_grab_frames;
    std::binary_semaphore is_activated{0};

    video_stream(int encChn, _stream *stream, const char *name)
        : encChn(encChn), stream(stream), name(name), running(false), idr(false), idr_fix(0), encoder(nullptr), imp_framesource(nullptr),
          msgChannel(std::make_shared<MsgChannel<H264NALUnit>>(MSG_CHANNEL_SIZE)), onDataCallback(nullptr),  run_for_jpeg{false},
          hasDataCallback{false} {}
};

struct backchannel_stream
{
    std::shared_ptr<MsgChannel<BackchannelFrame>> inputQueue;
    IMPBackchannel* imp_backchannel; // This remains for now
    bool running;
    pthread_t thread;
    std::mutex mutex;
    std::condition_variable should_grab_frames;
    std::atomic<unsigned int> is_sending{0};

    backchannel_stream()
        : inputQueue(std::make_shared<MsgChannel<BackchannelFrame>>(MSG_CHANNEL_SIZE)),
        imp_backchannel(nullptr),
        running(false) {}
};

extern std::condition_variable global_cv_worker_restart;
extern bool global_restart;
extern bool global_restart_rtsp;
extern bool global_restart_video;
extern bool global_restart_audio;
extern bool global_osd_thread_signal;
extern bool global_main_thread_signal;
extern bool global_motion_thread_signal;
extern std::atomic<char> global_rtsp_thread_signal;

extern std::shared_ptr<jpeg_stream> global_jpeg[NUM_VIDEO_CHANNELS];
extern std::shared_ptr<audio_stream> global_audio[NUM_AUDIO_CHANNELS];
extern std::shared_ptr<video_stream> global_video[NUM_VIDEO_CHANNELS];
extern std::shared_ptr<backchannel_stream> global_backchannel;

#endif // GLOBALS_HPP
