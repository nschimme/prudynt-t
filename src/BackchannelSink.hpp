#ifndef BACKCHANNEL_SINK_FRAMED_SOURCE_HPP // Changed guard name
#define BACKCHANNEL_SINK_FRAMED_SOURCE_HPP

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <list>
#include <map>
#include <vector> // Needed for MsgChannel template
#include <cstdint> // Needed for uint8_t
#include <atomic> // For std::atomic<bool>
#include <sys/time.h> // For struct timeval, gettimeofday

#include "Logger.hpp"
#include "MsgChannel.hpp" // Include MsgChannel definition
// #include "globals.hpp" // Removed include to break cycle
#include "IMPBackchannel.hpp" // Include definition for IMPBackchannelFormat
// #include "globals.hpp" // Removed include

// Forward declarations
class FramedSource; // Use FramedSource instead of RTPSource
class TaskScheduler; // For TaskToken
struct backchannel_stream; // Forward declaration
struct BackchannelFrame;   // Forward declaration

// Inherits from MediaSink as it consumes data from an RTPSource
class BackchannelSink : public MediaSink {
public:
    // Updated createNew signature to accept the expected audio format
    static BackchannelSink* createNew(UsageEnvironment& env, backchannel_stream* stream_data,
                                      unsigned clientSessionId, IMPBackchannelFormat format);

    // Method to start consuming from the source (now takes FramedSource)
    Boolean startPlaying(FramedSource& source,
                         MediaSink::afterPlayingFunc* afterFunc,
                         void* afterClientData);
    // Method to stop consuming (called by StreamState destructor)
    void stopPlaying();

    // Getter for client session ID
    unsigned getClientSessionId() const;

protected:
    // Updated constructor signature to accept the expected audio format
    BackchannelSink(UsageEnvironment& env, backchannel_stream* stream_data,
                    unsigned clientSessionId, IMPBackchannelFormat format);
    virtual ~BackchannelSink();

    // --- Required virtual methods from MediaSink ---
    virtual Boolean continuePlaying(); // Called when source has data

private:
    // Audio Data Timeout Timer logic functions (Simplified)
    void scheduleTimeoutCheck();
    static void timeoutCheck(void* clientData);
    void timeoutCheck1();

    // Static callback wrapper for incoming frames from the RTPSource
    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned durationInMicroseconds);
    // Per-instance handler
    void afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                            struct timeval presentationTime);

    // Static callback wrapper for source closure
    static void staticOnSourceClosure(void* clientData);
    // Per-instance handler for source closure
    void onSourceClosure1();

    // --- Members ---
    FramedSource* fRTPSource; // Changed type to FramedSource* (keeping name for now)
    u_int8_t* fReceiveBuffer; // Buffer to receive data
    static const unsigned kReceiveBufferSize = 2048; // Adjust as needed
    backchannel_stream* fStream; // Pointer to the shared stream state (queue, flags)
    MsgChannel<BackchannelFrame>* fInputQueue; // Pointer to the worker's input queue (Updated type)

    // State tracking
    Boolean fIsActive; // Are we currently playing/consuming?
    MediaSink::afterPlayingFunc* fAfterFunc; // Callback when source stops
    void* fAfterClientData; // Client data for the callback

    // --- Thread safety for shared audio output ---
    static std::atomic<bool> gIsAudioOutputBusy; // Shared flag for all instances
    bool fHaveAudioOutputLock;                   // Does this instance hold the lock?

    // --- Audio Data Timeout Timer Members ---
    unsigned fClientSessionId;                   // Client session ID for logging
    TaskToken fTimeoutTask;                      // Handle for the audio data timeout timer task
    // struct timeval fLastDataTime;             // Removed: Not needed with simplified logic
    static const unsigned kAudioDataTimeoutSeconds = 15; // Timeout in seconds

    // --- Stored Audio Properties ---
    const IMPBackchannelFormat fFormat;          // Expected audio format (passed during construction)
    // const unsigned fFrequency = 8000;         // Removed: Use constants from IMPBackchannel.hpp
};

#endif // BACKCHANNEL_SINK_FRAMED_SOURCE_HPP
