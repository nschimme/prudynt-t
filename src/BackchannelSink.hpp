#ifndef BACKCHANNEL_SINK_FRAMED_SOURCE_HPP
#define BACKCHANNEL_SINK_FRAMED_SOURCE_HPP

#include <BasicUsageEnvironment.hh>
#include <atomic> // Keep for std::atomic member
#include <cstdint>
// #include <list> // Cline: Commented out unused header
#include <liveMedia.hh>
// #include <map> // Cline: Commented out unused header
// #include <sys/time.h> // Cline: Commented out unused header (likely included via liveMedia)
#include <vector> // Keep for BackchannelFrame usage in cpp

#include "IMPBackchannel.hpp"
#include "Logger.hpp" // Keep for LOG_* usage in cpp
// #include "MsgChannel.hpp" // Cline: Commented out unused header

class FramedSource;
class TaskScheduler;
struct backchannel_stream;
struct BackchannelFrame;

class BackchannelSink : public MediaSink
{
public:
    static BackchannelSink *createNew(UsageEnvironment &env, unsigned clientSessionId,
                                      IMPBackchannelFormat format);

    Boolean startPlaying(FramedSource &source, MediaSink::afterPlayingFunc *afterFunc,
                         void *afterClientData);
    void stopPlaying();

    unsigned getClientSessionId() const;

protected:
    BackchannelSink(UsageEnvironment &env, unsigned clientSessionId, IMPBackchannelFormat format);
    virtual ~BackchannelSink();

    virtual Boolean continuePlaying();

private:
    void scheduleTimeoutCheck();
    static void timeoutCheck(void *clientData);
    void timeoutCheck1();

    static void afterGettingFrame(void *clientData, unsigned frameSize, unsigned numTruncatedBytes,
                                  struct timeval presentationTime, unsigned durationInMicroseconds);
    void afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                            struct timeval presentationTime);

    static void staticOnSourceClosure(void *clientData);
    void onSourceClosure1();

    // Helper functions for sending frames to the processor queue
    void sendBackchannelFrame(const uint8_t *payload,
                              unsigned payloadSize); // No timestamp param
    void sendBackchannelStopFrame();                 // Renamed from sendStopSignal

    FramedSource *fRTPSource;
    u_int8_t *fReceiveBuffer;
    int fReceiveBufferSize;

    Boolean fIsActive;
    MediaSink::afterPlayingFunc *fAfterFunc;
    void *fAfterClientData;

    static std::atomic<bool> gIsAudioOutputBusy;
    bool fHaveAudioOutputLock;

    unsigned fClientSessionId;
    TaskToken fTimeoutTask;

    const IMPBackchannelFormat fFormat;
};

#endif // BACKCHANNEL_SINK_FRAMED_SOURCE_HPP
