#ifndef BACKCHANNEL_SINK_FRAMED_SOURCE_HPP // Changed guard name
#define BACKCHANNEL_SINK_FRAMED_SOURCE_HPP

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <list>
#include <map>
#include <vector> // Needed for MsgChannel template
#include <cstdint> // Needed for uint8_t

#include "Logger.hpp"
#include "MsgChannel.hpp" // Include MsgChannel definition
#include "globals.hpp"    // Include globals to get backchannel_stream definition

// Renamed class, still inherits FramedSource as it acts as the source for the subsession
class BackchannelSink : public FramedSource {
public:
    // Constructor now takes the backchannel_stream pointer
    static BackchannelSink* createNew(UsageEnvironment& env, backchannel_stream* stream_data);

    // Methods to manage client sources
    virtual void addSource(FramedSource* source);
    virtual void removeSource(FramedSource* source);

protected:
    BackchannelSink(UsageEnvironment& env, backchannel_stream* stream_data); // Updated constructor
    virtual ~BackchannelSink();

private:
    // Helper struct for client source callbacks
    struct ClientSourceContext {
        BackchannelSink* sink; // Renamed from selector
        FramedSource* source;
    };

    // Static callback wrapper for frames coming *from* client sources (remains same)
    static void incomingDataHandler(void* clientData, unsigned frameSize,
                                    unsigned numTruncatedBytes,
                                    struct timeval presentationTime,
                                    unsigned durationInMicroseconds);
    // Per-instance handler for frames coming *from* client sources (updated signature)
    void handleIncomingData(ClientSourceContext* context, unsigned frameSize, unsigned numTruncatedBytes);

    // Static callback for client source closure (remains same)
    static void sourceClosureHandler(void* clientData);
    void handleSourceClosure(FramedSource* source);


    // Overridden virtual methods for FramedSource (These might become NO-OPs or simplified)
    virtual void doGetNextFrame(); // This source doesn't produce frames for Live555 pipeline
    virtual void doStopGettingFrames(); // Needs to stop requesting from clients

    // --- Members for managing client sources ---
    FramedSource* fControllingSource; // The client source currently allowed to send data
    std::list<FramedSource*> fClientSources; // Keep track of client sources for fallback
    std::map<FramedSource*, ClientSourceContext*> fSourceContexts; // Map sources to their callback contexts

    // --- Members for receiving data and queuing ---
    u_int8_t* fClientReceiveBuffer; // Buffer to receive data *from* clients
    static const unsigned kClientReceiveBufferSize = 2048; // Max expected RTP packet size? Adjust as needed.
    backchannel_stream* fStream; // Pointer to the shared stream state (queue, flags)
    MsgChannel<std::vector<uint8_t>>* fInputQueue; // Pointer to the worker's input queue (convenience, points into fStream)

    bool fIsCurrentlyGettingFrames; // Track if we should be actively getting frames from clients

    // --- Removed members related to downstream delivery ---
    // std::vector<u_int8_t> fLastDeliveredFrame;
    // unsigned fLastFrameSize;
    // struct timeval fLastPresentationTime;
    // unsigned fLastDurationInMicroseconds;
    // TaskToken fNextTask;
    // void deliverFrame();
};

#endif // BACKCHANNEL_SINK_FRAMED_SOURCE_HPP
