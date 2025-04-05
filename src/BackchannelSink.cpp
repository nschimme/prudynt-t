#include "BackchannelSink.hpp"
#include "Logger.hpp"
#include "globals.hpp" // For backchannel_stream

// Live555 Headers
#include <GroupsockHelper.hh> // For closeSocket if needed, maybe not
#include <RTPSource.hh>
#include <vector>

#define MODULE "BackchannelSink"

BackchannelSink* BackchannelSink::createNew(UsageEnvironment& env, backchannel_stream* stream_data) {
    return new BackchannelSink(env, stream_data);
}

BackchannelSink::BackchannelSink(UsageEnvironment& env, backchannel_stream* stream_data)
    : MediaSink(env), // Call base class constructor
      fRTPSource(nullptr),
      fStream(stream_data),
      fInputQueue(nullptr),
      fIsActive(False),
      fAfterFunc(nullptr),
      fAfterClientData(nullptr)
{
    LOG_DEBUG("BackchannelSink created (as MediaSink)");
    if (fStream == nullptr) {
         LOG_ERROR("backchannel_stream provided to BackchannelSink is null!");
         // Handle error - maybe assert?
    } else {
        fInputQueue = fStream->inputQueue.get();
        if (fInputQueue == nullptr) {
             LOG_ERROR("Input queue within backchannel_stream is null!");
             // Handle error
        }
    }
    fReceiveBuffer = new u_int8_t[kReceiveBufferSize];
    if (fReceiveBuffer == nullptr) {
        LOG_ERROR("Failed to allocate receive buffer");
        // Consider throwing an exception or handling error more robustly
    }
}

BackchannelSink::~BackchannelSink() {
    LOG_DEBUG("BackchannelSink destroyed");
    // Ensure we stop playing if active (should be stopped by StreamState already)
    stopPlaying();
    delete[] fReceiveBuffer;
    // fInputQueue and fStream are managed externally
}

Boolean BackchannelSink::startPlaying(RTPSource& rtpSource,
                                      MediaSink::afterPlayingFunc* afterFunc,
                                      void* afterClientData)
{
    if (fIsActive) {
        LOG_WARN("startPlaying called while already active");
        return False; // Already playing
    }

    fRTPSource = &rtpSource;
    fAfterFunc = afterFunc;
    fAfterClientData = afterClientData;
    fIsActive = True;

    // Increment active session count using fStream (atomically)
    // Do this *before* starting to get frames
    if (fStream) {
        int previous_count = fStream->active_sessions.fetch_add(1, std::memory_order_relaxed);
        LOG_INFO("Backchannel sink starting. Active sessions: " << previous_count + 1);
    } else {
        LOG_ERROR("fStream is null in startPlaying! Cannot increment session count.");
    }


    LOG_INFO("BackchannelSink starting to play/consume from RTPSource");
    // Start the process by requesting the first frame
    return continuePlaying();
}

void BackchannelSink::stopPlaying() {
    if (!fIsActive) {
        // LOG_DEBUG("stopPlaying called while not active"); // Can be noisy
        return;
    }

    LOG_INFO("BackchannelSink stopping play/consumption");
    fIsActive = False;

    // Stop the source from delivering frames
    if (fRTPSource != nullptr) {
        fRTPSource->stopGettingFrames();
    }

    // Decrement active session count (atomically)
    // Do this *after* stopping frames
    if (fStream) {
        int previous_count = fStream->active_sessions.fetch_sub(1, std::memory_order_relaxed);
        LOG_INFO("Backchannel sink stopped. Active sessions: " << previous_count - 1);
         // Optional: Add logic here if previous_count was 1 (last session ended)
    } else {
         LOG_ERROR("fStream is null in stopPlaying! Cannot decrement session count.");
    }


    // Call the afterPlaying function if specified (though usually null for sinks)
    if (fAfterFunc != nullptr) {
        (*fAfterFunc)(fAfterClientData);
    }

    // Clear pointers after stopping
    fRTPSource = nullptr;
    fAfterFunc = nullptr;
    fAfterClientData = nullptr;
}


// Called by Live555 when the source has data or closes
Boolean BackchannelSink::continuePlaying() {
    if (!fIsActive || fRTPSource == nullptr) {
        return False; // Not active or source detached
    }

    // Request the next frame from the source
    fRTPSource->getNextFrame(fReceiveBuffer, kReceiveBufferSize,
                             afterGettingFrame, this,
                             onSourceClosure, this); // Use static wrappers

    return True; // Indicate we want to continue
}

// Static callback wrapper for incoming frames from the RTPSource
void BackchannelSink::afterGettingFrame(void* clientData, unsigned frameSize,
                                        unsigned numTruncatedBytes,
                                        struct timeval presentationTime,
                                        unsigned /*durationInMicroseconds*/) // Duration often 0 for RTP
{
    BackchannelSink* sink = static_cast<BackchannelSink*>(clientData);
    if (sink != nullptr) {
        sink->afterGettingFrame1(frameSize, numTruncatedBytes, presentationTime);
    } else {
        LOG_ERROR("afterGettingFrame called with invalid clientData");
    }
}

// Per-instance handler for incoming frames
void BackchannelSink::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                                         struct timeval /*presentationTime*/)
{
    if (!fIsActive) {
        // LOG_DEBUG("Frame received but sink is no longer active");
        return; // No longer playing
    }

    // Process the received frame
    if (numTruncatedBytes > 0) {
        LOG_WARN("Received truncated frame (" << frameSize << " bytes, " << numTruncatedBytes << " truncated). Discarding.");
    } else if (frameSize == 0) {
        // LOG_DEBUG("Received zero-sized frame. Ignoring."); // Can be noisy
    } else {
        // Valid frame received
        // LOG_DEBUG("Received frame of size " << frameSize); // Can be very noisy
        if (fInputQueue) {
            // Copy data to a vector and queue it
            std::vector<uint8_t> rtpPacket(fReceiveBuffer, fReceiveBuffer + frameSize);
            if (!fInputQueue->write(std::move(rtpPacket))) {
                 LOG_WARN("Backchannel input queue was full. Oldest packet dropped.");
            }
        } else {
             LOG_ERROR("Input queue is null, cannot queue packet!");
        }
    }

    // Immediately request the next frame to keep the data flowing
    if (fIsActive) {
        continuePlaying();
    }
}

// Static callback for source closure (called by Live555 via getNextFrame)
void MediaSink::onSourceClosure(void* clientData) {
    BackchannelSink* sink = static_cast<BackchannelSink*>(clientData);
    if (sink != nullptr) {
        LOG_INFO("Source closure detected by BackchannelSink");
        // The source has closed (e.g., client disconnected sending RTCP BYE)
        // We should stop playing, which will also call the afterPlaying function if set.
        // Note: Don't call stopPlaying directly from here if it might delete the sink
        //       or cause re-entrancy issues. Schedule it instead.
        sink->envir().taskScheduler().scheduleDelayedTask(0,
            (TaskFunc*)[](void* cd) {
                BackchannelSink* s = static_cast<BackchannelSink*>(cd);
                if (s) s->stopPlaying();
            },
            sink);
    } else {
         LOG_ERROR("onSourceClosure called with invalid clientData");
    }
}
