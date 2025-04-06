#ifndef BACKCHANNEL_SINK_FRAMED_SOURCE_HPP
#define BACKCHANNEL_SINK_FRAMED_SOURCE_HPP

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <list>
#include <map>
#include <vector>
#include <cstdint>
#include <atomic>
#include <sys/time.h>

#include "Logger.hpp"
#include "MsgChannel.hpp"
#include "IMPBackchannel.hpp"

class FramedSource;
class TaskScheduler;
struct backchannel_stream;
struct BackchannelFrame;

class BackchannelSink : public MediaSink {
public:
    static BackchannelSink* createNew(UsageEnvironment& env, backchannel_stream* stream_data,
                                      unsigned clientSessionId, IMPBackchannelFormat format);

    Boolean startPlaying(FramedSource& source,
                         MediaSink::afterPlayingFunc* afterFunc,
                         void* afterClientData);
    void stopPlaying();

    unsigned getClientSessionId() const;

protected:
    BackchannelSink(UsageEnvironment& env, backchannel_stream* stream_data,
                    unsigned clientSessionId, IMPBackchannelFormat format);
    virtual ~BackchannelSink();

    virtual Boolean continuePlaying();

private:
    void scheduleTimeoutCheck();
    static void timeoutCheck(void* clientData);
    void timeoutCheck1();

    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned durationInMicroseconds);
    void afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                            struct timeval presentationTime);

    static void staticOnSourceClosure(void* clientData);
    void onSourceClosure1();

    FramedSource* fRTPSource;
    u_int8_t* fReceiveBuffer;
    static const unsigned kReceiveBufferSize = 2048;
    backchannel_stream* fStream;
    MsgChannel<BackchannelFrame>* fInputQueue;

    Boolean fIsActive;
    MediaSink::afterPlayingFunc* fAfterFunc;
    void* fAfterClientData;

    static std::atomic<bool> gIsAudioOutputBusy;
    bool fHaveAudioOutputLock;

    unsigned fClientSessionId;
    TaskToken fTimeoutTask;
    static const unsigned kAudioDataTimeoutSeconds = 15;

    const IMPBackchannelFormat fFormat;
};

#endif // BACKCHANNEL_SINK_FRAMED_SOURCE_HPP
