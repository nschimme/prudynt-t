#include "BackchannelSink.hpp"

#include "Logger.hpp"
#include "globals.hpp"

#include <RTPSource.hh>
#include <vector>

#define MODULE "BackchannelSink"

#define TIMEOUT_MICROSECONDS 500000 // Timeout set to 500ms

BackchannelSink *BackchannelSink::createNew(UsageEnvironment &env,
                                            unsigned clientSessionId,
                                            IMPBackchannelFormat format)
{
    return new BackchannelSink(env, clientSessionId, format);
}

BackchannelSink::BackchannelSink(UsageEnvironment &env,
                                 unsigned clientSessionId,
                                 IMPBackchannelFormat format)
    : MediaSink(env)
    , fRTPSource(nullptr)
    , fReceiveBufferSize((format == IMPBackchannelFormat::OPUS) ? 2048 : 1024)
    , fIsActive(False)
    , fAfterFunc(nullptr)
    , fAfterClientData(nullptr)
    , fClientSessionId(clientSessionId)
    , fTimeoutTask(nullptr)
    , fFormat(format)
{
    LOG_DEBUG("Sink created for session " << fClientSessionId << " format "
                                          << static_cast<int>(fFormat));
    fReceiveBuffer = new u_int8_t[fReceiveBufferSize];
    if (fReceiveBuffer == nullptr)
    {
        LOG_ERROR("Failed to allocate receive buffer (Session: " << fClientSessionId << ")");
    }
}

BackchannelSink::~BackchannelSink()
{
    LOG_DEBUG("Sink destroyed for session " << fClientSessionId);
    stopPlaying();
    delete[] fReceiveBuffer;
}

Boolean BackchannelSink::startPlaying(FramedSource &source,
                                      MediaSink::afterPlayingFunc *afterFunc,
                                      void *afterClientData)
{
    if (fIsActive)
    {
        LOG_WARN("startPlaying called while already active for session " << fClientSessionId);
        return False;
    }

    fRTPSource = &source;
    fAfterFunc = afterFunc;
    fAfterClientData = afterClientData;
    fIsActive = True;

    LOG_DEBUG("Sink starting consumption for session " << fClientSessionId);

    return continuePlaying();
}

void BackchannelSink::stopPlaying()
{
    if (!fIsActive)
    {
        return;
    }

    LOG_DEBUG("Sink stopping consumption for session " << fClientSessionId);

    // Set inactive *first* to prevent re-entrancy
    fIsActive = False;

    // Send the stop signal exactly once when stopping an active sink
    sendBackchannelStopFrame();

    envir().taskScheduler().unscheduleDelayedTask(fTimeoutTask);
    fTimeoutTask = nullptr;

    if (fRTPSource != nullptr)
    {
        fRTPSource->stopGettingFrames();
    }

    if (fAfterFunc != nullptr)
    {
        (*fAfterFunc)(fAfterClientData);
    }

    fRTPSource = nullptr;
    fAfterFunc = nullptr;
    fAfterClientData = nullptr;
}

unsigned BackchannelSink::getClientSessionId() const
{
    return fClientSessionId;
}

Boolean BackchannelSink::continuePlaying()
{
    if (!fIsActive || fRTPSource == nullptr)
    {
        return False;
    }

    fRTPSource->getNextFrame(fReceiveBuffer,
                             fReceiveBufferSize,
                             afterGettingFrame,
                             this,
                             staticOnSourceClosure,
                             this);

    return True;
}

void BackchannelSink::afterGettingFrame(void *clientData,
                                        unsigned frameSize,
                                        unsigned numTruncatedBytes,
                                        struct timeval presentationTime,
                                        unsigned /*durationInMicroseconds*/)
{
    BackchannelSink *sink = static_cast<BackchannelSink *>(clientData);
    if (sink != nullptr)
    {
        sink->afterGettingFrame1(frameSize, numTruncatedBytes, presentationTime);
    }
    else
    {
        LOG_ERROR("afterGettingFrame called with invalid clientData");
    }
}

void BackchannelSink::afterGettingFrame1(unsigned frameSize,
                                         unsigned numTruncatedBytes,
                                         struct timeval presentationTime)
{
    if (!fIsActive)
    {
        return;
    }

    if (numTruncatedBytes > 0)
    {
        LOG_WARN("Received truncated frame (" << frameSize << " bytes, " << numTruncatedBytes
                                              << " truncated) for session " << fClientSessionId
                                              << ". Discarding.");
    }
    else if (frameSize > 0)
    {
        sendBackchannelFrame(fReceiveBuffer, frameSize);
    }

    // Reschedule the timeout check after receiving any frame (even size 0 or
    // truncated) This resets the timer as long as *something* is coming from the
    // source.
    envir().taskScheduler().unscheduleDelayedTask(fTimeoutTask);
    scheduleTimeoutCheck();

    if (fIsActive)
    {
        continuePlaying();
    }
}

void BackchannelSink::scheduleTimeoutCheck()
{
    fTimeoutTask = envir().taskScheduler().scheduleDelayedTask(TIMEOUT_MICROSECONDS,
                                                               (TaskFunc *) timeoutCheck,
                                                               this);
}

void BackchannelSink::timeoutCheck(void *clientData)
{
    BackchannelSink *sink = static_cast<BackchannelSink *>(clientData);
    if (sink)
    {
        sink->timeoutCheck1();
    }
}

void BackchannelSink::timeoutCheck1()
{
    fTimeoutTask = nullptr;

    if (!fIsActive)
    {
        return;
    }

    LOG_INFO("Audio data timeout detected for session "
             << fClientSessionId << ". Sending stop signal and stopping sink.");
    sendBackchannelStopFrame();
}

void BackchannelSink::sendBackchannelFrame(const uint8_t *payload, unsigned payloadSize)
{
    if (global_backchannel && global_backchannel->inputQueue)
    {
        BackchannelFrame bcFrame;
        bcFrame.format = fFormat;
        bcFrame.clientSessionId = fClientSessionId;
        bcFrame.payload.assign(payload, payload + payloadSize);

        if (!global_backchannel->inputQueue->write(std::move(bcFrame)))
        {
            LOG_WARN("Input queue full for session " << fClientSessionId << ". Frame dropped.");
        }
    }
    else
    {
        LOG_ERROR("global_backchannel or its input queue is null, cannot queue "
                  "BackchannelFrame! (Session: "
                  << fClientSessionId << ")");
    }
}

void BackchannelSink::sendBackchannelStopFrame()
{
    if (global_backchannel && global_backchannel->inputQueue)
    {
        BackchannelFrame stopFrame;
        stopFrame.format = fFormat;
        stopFrame.clientSessionId = fClientSessionId;
        stopFrame.payload.clear(); // Zero-size payload indicates stop/timeout
        if (!global_backchannel->inputQueue->write(std::move(stopFrame)))
        {
            LOG_WARN("Input queue full when trying to send stop signal for session "
                     << fClientSessionId);
        }
        else
        {
            LOG_INFO("Sent stop signal (zero-payload frame) for session " << fClientSessionId);
        }
    }
    else
    {
        LOG_ERROR("global_backchannel or input queue null, cannot send stop signal for "
                  "session "
                  << fClientSessionId);
    }
}

void BackchannelSink::staticOnSourceClosure(void *clientData)
{
    BackchannelSink *sink = static_cast<BackchannelSink *>(clientData);
    if (sink != nullptr)
    {
        sink->stopPlaying();
    }
    else
    {
        LOG_ERROR("staticOnSourceClosure called with invalid clientData");
    }
}

void BackchannelSink::onSourceClosure1()
{
    LOG_DEBUG("Source closure detected for session " << getClientSessionId()
                                                     << ". Scheduling stop.");
    stopPlaying();
}
