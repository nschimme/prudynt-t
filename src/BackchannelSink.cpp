#include "BackchannelSink.hpp"
#include "Logger.hpp"
#include "globals.hpp"

#include <sys/time.h>

#include <RTPSource.hh>
#include <vector>
#include <atomic>
#include <arpa/inet.h>
#include <sstream>
#include <iomanip>

#define MODULE "BackchannelSink"


std::atomic<bool> BackchannelSink::gIsAudioOutputBusy{false};

BackchannelSink* BackchannelSink::createNew(UsageEnvironment& env, backchannel_stream* stream_data,
                                            unsigned clientSessionId, IMPBackchannelFormat format) {
    return new BackchannelSink(env, stream_data, clientSessionId, format);
}

BackchannelSink::BackchannelSink(UsageEnvironment& env, backchannel_stream* stream_data,
                                 unsigned clientSessionId, IMPBackchannelFormat format)
    : MediaSink(env),
      fRTPSource(nullptr),
      fStream(stream_data),
      fInputQueue(nullptr),
      fIsActive(False),
      fAfterFunc(nullptr),
      fAfterClientData(nullptr),
      fHaveAudioOutputLock(false),
      fClientSessionId(clientSessionId),
      fTimeoutTask(nullptr),
      fFormat(format)
{
    LOG_DEBUG("Sink created for session " << fClientSessionId << " format " << static_cast<int>(fFormat));
    if (fStream == nullptr) {
         LOG_ERROR("backchannel_stream provided to BackchannelSink is null! (Session: " << fClientSessionId << ")");
    } else {
        fInputQueue = fStream->inputQueue.get();
        if (fInputQueue == nullptr) {
             LOG_ERROR("Input queue within backchannel_stream is null! (Session: " << fClientSessionId << ")");
        }
    }
    fReceiveBuffer = new u_int8_t[kReceiveBufferSize];
    if (fReceiveBuffer == nullptr) {
        LOG_ERROR("Failed to allocate receive buffer (Session: " << fClientSessionId << ")");
    }
}

BackchannelSink::~BackchannelSink() {
    LOG_DEBUG("Sink destroyed for session " << fClientSessionId);
    stopPlaying();
    delete[] fReceiveBuffer;
}

Boolean BackchannelSink::startPlaying(FramedSource& source,
                                      MediaSink::afterPlayingFunc* afterFunc,
                                      void* afterClientData)
{
    if (fIsActive) {
        LOG_WARN("startPlaying called while already active for session " << fClientSessionId);
        return False;
    }

    fRTPSource = &source;
    fAfterFunc = afterFunc;
    fAfterClientData = afterClientData;
    fHaveAudioOutputLock = false;
    fIsActive = True;

    if (fStream) {
        int previous_count = fStream->active_sessions.fetch_add(1, std::memory_order_relaxed);
        LOG_INFO("Sink starting for session " << fClientSessionId << ". Active sessions: " << previous_count + 1);
    } else {
        LOG_ERROR("fStream is null in startPlaying! Cannot increment session count. (Session: " << fClientSessionId << ")");
    }


    LOG_INFO("Sink starting consumption for session " << fClientSessionId);
    return continuePlaying();
}

void BackchannelSink::stopPlaying() {
    if (!fIsActive) {
        return;
    }

    LOG_INFO("Sink stopping consumption for session " << fClientSessionId);

    envir().taskScheduler().unscheduleDelayedTask(fTimeoutTask);
    fTimeoutTask = nullptr;

    fIsActive = False;

    if (fRTPSource != nullptr) {
        fRTPSource->stopGettingFrames();
    }

    if (fHaveAudioOutputLock) {
        gIsAudioOutputBusy.store(false);
        fHaveAudioOutputLock = false;
        LOG_INFO("Released audio lock during stopPlaying for session " << fClientSessionId);
    }

    if (fStream) {
        int previous_count = fStream->active_sessions.fetch_sub(1, std::memory_order_relaxed);
        LOG_INFO("Sink stopped for session " << fClientSessionId << ". Active sessions: " << previous_count - 1);
    } else {
         LOG_ERROR("fStream is null in stopPlaying! Cannot decrement session count. (Session: " << fClientSessionId << ")");
    }

    if (fAfterFunc != nullptr) {
        (*fAfterFunc)(fAfterClientData);
    }

    fRTPSource = nullptr;
    fAfterFunc = nullptr;
    fAfterClientData = nullptr;
}

unsigned BackchannelSink::getClientSessionId() const {
    return fClientSessionId;
}


Boolean BackchannelSink::continuePlaying() {
    if (!fIsActive || fRTPSource == nullptr) {
        return False;
    }

    fRTPSource->getNextFrame(fReceiveBuffer, kReceiveBufferSize,
                             afterGettingFrame, this,
                             staticOnSourceClosure, this);

    return True;
}

void BackchannelSink::afterGettingFrame(void* clientData, unsigned frameSize,
                                        unsigned numTruncatedBytes,
                                         struct timeval presentationTime,
                                         unsigned /*durationInMicroseconds*/)
{
    BackchannelSink* sink = static_cast<BackchannelSink*>(clientData);
    if (sink != nullptr) {
        sink->afterGettingFrame1(frameSize, numTruncatedBytes, presentationTime);
    } else {
        LOG_ERROR("afterGettingFrame called with invalid clientData");
    }
}


void BackchannelSink::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                                         struct timeval presentationTime)
{
    if (!fIsActive) {
        return;
    }

    if (frameSize > 0 && numTruncatedBytes == 0 && !fHaveAudioOutputLock) {
        bool expected = false;
        if (gIsAudioOutputBusy.compare_exchange_strong(expected, true)) {
            fHaveAudioOutputLock = true;
            LOG_INFO("Acquired audio lock on first frame for session " << fClientSessionId);

            LOG_DEBUG("Starting audio timeout timer for session " << fClientSessionId);
            scheduleTimeoutCheck();

        } else {
            fHaveAudioOutputLock = false;
            LOG_WARN("Failed to acquire audio lock (busy) for session " << fClientSessionId << ". Data received but not played.");
        }
    }

    if (numTruncatedBytes > 0) {
        LOG_WARN("Received truncated frame (" << frameSize << " bytes, " << numTruncatedBytes << " truncated) for session " << fClientSessionId << ". Discarding.");
    } else if (frameSize > 0) {

        BackchannelFrame bcFrame;
        bcFrame.format = fFormat;
        bcFrame.timestamp = presentationTime;
        bcFrame.payload.assign(fReceiveBuffer, fReceiveBuffer + frameSize);

        if (fHaveAudioOutputLock) {
            if (fInputQueue) {
                if (!fInputQueue->write(std::move(bcFrame))) {
                     LOG_WARN("Input queue full for session " << fClientSessionId << ". Frame dropped.");
                }
            } else {
                 LOG_ERROR("Input queue is null, cannot queue BackchannelFrame! (Session: " << fClientSessionId << ")");
            }
        } else {
        }
    }

    if (fTimeoutTask != nullptr || fHaveAudioOutputLock) {
        envir().taskScheduler().unscheduleDelayedTask(fTimeoutTask);
        scheduleTimeoutCheck();
    }

    if (fIsActive) {
        continuePlaying();
    }
}


void BackchannelSink::scheduleTimeoutCheck() {
    fTimeoutTask = envir().taskScheduler().scheduleDelayedTask(
        kAudioDataTimeoutSeconds * 1000000,
        (TaskFunc*)timeoutCheck,
        this);
}

void BackchannelSink::timeoutCheck(void* clientData) {
    BackchannelSink* sink = static_cast<BackchannelSink*>(clientData);
    if (sink) {
        sink->timeoutCheck1();
    }
}

void BackchannelSink::timeoutCheck1() {
    fTimeoutTask = nullptr;

    if (!fIsActive) {
        return;
    }


    LOG_WARN("Audio data timeout detected for session " << fClientSessionId << " (>" << kAudioDataTimeoutSeconds << "s).");

    if (fHaveAudioOutputLock) {
        LOG_WARN("Releasing audio lock due to timeout for session " << fClientSessionId << ".");
        gIsAudioOutputBusy.store(false);
        fHaveAudioOutputLock = false;
    } else {
    }
}


void BackchannelSink::staticOnSourceClosure(void* clientData) {
    BackchannelSink* sink = static_cast<BackchannelSink*>(clientData);
    if (sink != nullptr) {
        sink->onSourceClosure1();
    } else {
         LOG_ERROR("staticOnSourceClosure called with invalid clientData");
    }
}

void BackchannelSink::onSourceClosure1() {
    LOG_INFO("Source closure detected for session " << getClientSessionId());
    envir().taskScheduler().scheduleDelayedTask(0,
        (TaskFunc*)[](void* cd) {
            BackchannelSink* s = static_cast<BackchannelSink*>(cd);
            if (s) s->stopPlaying();
        },
        this);
}
