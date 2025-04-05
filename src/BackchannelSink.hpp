#ifndef BACKCHANNEL_SINK_FRAMED_SOURCE_HPP // Changed guard name
#define BACKCHANNEL_SINK_FRAMED_SOURCE_HPP

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <list>
#include <map>
#include <vector> // Needed for MsgChannel template
#include <cstdint> // Needed for uint8_t
#include <atomic> // For std::atomic<bool>

#include "Logger.hpp"
#include "MsgChannel.hpp" // Include MsgChannel definition
#include "globals.hpp"    // Include globals to get backchannel_stream definition

// Forward declarations
class RTPSource; // The source we will consume from

// Inherits from MediaSink as it consumes data from an RTPSource
class BackchannelSink : public MediaSink {
public:
    static BackchannelSink* createNew(UsageEnvironment& env, backchannel_stream* stream_data);

    // Method to start consuming from the source
    Boolean startPlaying(RTPSource& rtpSource,
                         MediaSink::afterPlayingFunc* afterFunc,
                         void* afterClientData);
    // Method to stop consuming (called by StreamState destructor)
    void stopPlaying();


protected:
    BackchannelSink(UsageEnvironment& env, backchannel_stream* stream_data);
    virtual ~BackchannelSink();

    // --- Required virtual methods from MediaSink ---
    virtual Boolean continuePlaying(); // Called when source has data

private:
    // Static callback wrapper for incoming frames from the RTPSource
    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned durationInMicroseconds);
    // Per-instance handler
    void afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                            struct timeval presentationTime);

    // --- Members ---
    RTPSource* fRTPSource; // The source providing data
    u_int8_t* fReceiveBuffer; // Buffer to receive data
    static const unsigned kReceiveBufferSize = 2048; // Adjust as needed
    backchannel_stream* fStream; // Pointer to the shared stream state (queue, flags)
    MsgChannel<std::vector<uint8_t>>* fInputQueue; // Pointer to the worker's input queue

    // State tracking
    Boolean fIsActive; // Are we currently playing/consuming?
    MediaSink::afterPlayingFunc* fAfterFunc; // Callback when source stops
    void* fAfterClientData; // Client data for the callback

    // --- Thread safety for shared audio output ---
    static std::atomic<bool> gIsAudioOutputBusy; // Shared flag for all instances
    bool fHaveAudioOutputLock;                   // Does this instance hold the lock?
};

#endif // BACKCHANNEL_SINK_FRAMED_SOURCE_HPP
