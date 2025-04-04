#ifndef BACKCHANNEL_SINK_HPP
#define BACKCHANNEL_SINK_HPP

// Standard C/C++ Headers
#include <cstdio> // For FILE* (popen)
#include <unistd.h> // For popen/pclose
#include <errno.h> // For error checking
#include <list>
#include <map>

// Live555 Headers
#include "MediaSink.hh"
#include "Boolean.hh"
#include "FramedSource.hh"

// Project Headers
#include "Logger.hpp"

class BackchannelSink : public MediaSink {
    // Helper struct to pass both sink and source to the callback
    struct BackchannelClientData {
        BackchannelSink* sink;
        FramedSource* source;
    };

public:
    static BackchannelSink* createNew(UsageEnvironment& env);

    virtual void addSource(FramedSource* source);
    virtual void removeSource(FramedSource* source);

protected:
    BackchannelSink(UsageEnvironment& env);
    virtual ~BackchannelSink();

    // Static callback wrapper
    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned durationInMicroseconds);
    // Per-instance frame handler - now takes context
    virtual void afterGettingFrame(BackchannelClientData* context, unsigned frameSize, unsigned numTruncatedBytes,
                                   struct timeval presentationTime, unsigned durationInMicroseconds);

    // Implementation of virtual functions:
    virtual Boolean continuePlaying();
    virtual void stopPlaying();

private:
    // Methods for managing the pipe
    bool startPipe();
    void closePipe();
    bool writeToPipe(u_int8_t* data, unsigned size);

    // --- Members for the "last source controls" logic with fallback ---
    FramedSource* fControllingSource; // The source currently allowed to send data
    std::list<FramedSource*> fClientSources; // Keep track of client sources in order of addition for fallback
    std::map<FramedSource*, BackchannelClientData*> fSourceContexts; // Map sources to their callback contexts
    // --- End of control logic members ---

    u_int8_t* fReceiveBuffer;
    static const unsigned kReceiveBufferSize = 1024; // Adjust as needed

    // Member for the pipe
    FILE* fPipe; // File pointer for the popen stream

    // State variable to track if playing
    bool fIsPlaying;
};

#endif // BACKCHANNEL_SINK_HPP
