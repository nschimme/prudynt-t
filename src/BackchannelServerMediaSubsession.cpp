#include "BackchannelServerMediaSubsession.hpp"
#include "SimpleRTPSink.hh" // A standard sink for receiving RTP
#include "BasicUsageEnvironment.hh"
#include <stdio.h> // For sprintf

// Define the track ID string (must match CustomRTSPClientSession.cpp)
// This definition makes the extern declaration in the header work.
extern const char* BACKCHANNEL_TRACK_ID = "track_backchannel";

BackchannelServerMediaSubsession* BackchannelServerMediaSubsession::createNew(UsageEnvironment& env) {
    return new BackchannelServerMediaSubsession(env);
}

BackchannelServerMediaSubsession::BackchannelServerMediaSubsession(UsageEnvironment& env)
    : OnDemandServerMediaSubsession(env, True /* reuse first source */), fSDPLines(nullptr) {
    // Constructor: Initialize members, maybe generate SDP lines here or lazily in sdpLines()
}

BackchannelServerMediaSubsession::~BackchannelServerMediaSubsession() {
    delete[] fSDPLines;
}

// Generate the SDP description for this subsession
char const* BackchannelServerMediaSubsession::sdpLines() {
    if (fSDPLines == nullptr) {
        // We need to generate the SDP lines which describe this stream.
        // This includes media type, payload format, clock rate, and direction.

        // Allocate a buffer for the SDP lines. Estimate size needed.
        unsigned int sdpLinesSize = 200; // Adjust size as needed
        fSDPLines = new char[sdpLinesSize];
        if (fSDPLines == nullptr) return nullptr; // Allocation failed

        // Get the RTP payload type for PCMA (usually 8)
        unsigned char rtpPayloadType = 8; // PCMA

        // Create the SDP lines string
        // m=<media> <port> <proto> <fmt> ...
        // Port 0 means the client chooses the port.
        snprintf(fSDPLines, sdpLinesSize,
                 "m=audio 0 RTP/AVP %d\r\n"
                 "c=IN IP4 0.0.0.0\r\n" // Connection address (0.0.0.0 allows any interface)
                 "b=AS:64\r\n"         // Bandwidth indication (optional, 64 kbps for G.711)
                 "a=rtpmap:%d PCMA/8000\r\n" // Payload type mapping
                 "a=control:%s\r\n"         // Track control identifier
                 "a=recvonly\r\n",          // Indicates server only receives on this stream
                 rtpPayloadType,
                 rtpPayloadType,
                 trackId() // Use the trackId() method which should return BACKCHANNEL_TRACK_ID
        );

        // Ensure null termination just in case snprintf truncated
        fSDPLines[sdpLinesSize - 1] = '\0';
    }
    return fSDPLines;
}

// Create the sink that will receive RTP data from the client
RTPSink* BackchannelServerMediaSubsession::createNewRTPSink(Groupsock* rtpGroupsock,
                                                          unsigned char rtpPayloadTypeIfDynamic,
                                                          FramedSource* /*inputSource*/) {
    // We are receiving, not sending, so inputSource is irrelevant here.
    // We need a sink to receive the RTP packets. SimpleRTPSink is suitable.
    // The payload type should be static (8 for PCMA), not dynamic.
    unsigned char rtpPayloadType = 8; // PCMA
    unsigned estimatedBitrate = 64000; // 8000 samples/sec * 8 bits/sample

    return SimpleRTPSink::createNew(envir(), rtpGroupsock,
                                    rtpPayloadType, 8000, // Payload type, clock frequency
                                    "audio", "PCMA",      // Media type, codec name
                                    1, True, False);      // Num channels, allow interleaving, index is RTCP
                                    // The last two params might need adjustment based on specific client needs
}

// Create the source for the stream (N/A for receive-only)
FramedSource* BackchannelServerMediaSubsession::createNewStreamSource(unsigned /*clientSessionId*/,
                                                                    unsigned& estBitrate) {
    // This subsession represents data received *from* the client.
    // There is no server-side source providing data *to* the sink.
    estBitrate = 0;
    return NULL;
}

// Override trackId() to return our specific identifier
// This is crucial for CustomRTSPClientSession to identify this track
char const* BackchannelServerMediaSubsession::trackId() {
    return BACKCHANNEL_TRACK_ID;
}
