// Project Headers
#include "BackchannelServerMediaSubsession.hpp"
#include "BackchannelSink.hpp" // Use the renamed sink
#include "Logger.hpp"   // For logging

// Live555 Headers
#include "BasicUsageEnvironment.hh"
#include "RTPSource.hh" // Needed for RTPSource type definition

// Standard C/C++ Headers
// (None needed directly in this file after cleanup)

#define MODULE "BackchannelSubsession" // Logger module name

BackchannelServerMediaSubsession* BackchannelServerMediaSubsession::createNew(UsageEnvironment& env, BackchannelSink* sink) { // Updated type
    return new BackchannelServerMediaSubsession(env, sink);
}

BackchannelServerMediaSubsession::BackchannelServerMediaSubsession(UsageEnvironment& env, BackchannelSink* sink) // Updated type
    : OnDemandServerMediaSubsession(env, True /*reuseFirstSource*/), fSDPLines(nullptr), fMySink(sink) { // Renamed member
    LOG_DEBUG("BackchannelServerMediaSubsession created");
    if (fMySink == nullptr) { // Use fMySink
        LOG_ERROR("BackchannelSink provided to BackchannelServerMediaSubsession is NULL!"); // Updated log
        // Handle error appropriately
    }
}

BackchannelServerMediaSubsession::~BackchannelServerMediaSubsession() {
    LOG_DEBUG("BackchannelServerMediaSubsession destroyed");
    delete[] fSDPLines;
}

// Generate the SDP description for this subsession
char const* BackchannelServerMediaSubsession::sdpLines() {
    if (fSDPLines == nullptr) {
        LOG_DEBUG("Generating SDP lines for backchannel");

         // Increase buffer size slightly for the extra rtpmap line
         unsigned int sdpLinesSize = 250;
         fSDPLines = new char[sdpLinesSize];
         if (fSDPLines == nullptr) return nullptr;

         // Define payload types
         const unsigned char pcmuPayloadType = 0;
         const unsigned char pcmaPayloadType = 8;

         // Generate SDP lines offering both PCMU and PCMA
         snprintf(fSDPLines, sdpLinesSize,
                  "m=audio 0 RTP/AVP %d %d\r\n" // Offer both payload types
                  "c=IN IP4 0.0.0.0\r\n"
                  "b=AS:64\r\n" // Adjust bandwidth estimate if needed
                  "a=rtpmap:%d PCMU/8000/1\r\n" // Map payload type 0 to PCMU
                  "a=rtpmap:%d PCMA/8000/1\r\n" // Map payload type 8 to PCMA
                  "a=control:%s\r\n"
                  "a=recvonly\r\n",
                  pcmuPayloadType, pcmaPayloadType, // Payload types for m= line
                  pcmuPayloadType,                 // Payload type for PCMU rtpmap
                  pcmaPayloadType,                 // Payload type for PCMA rtpmap
                  trackId()                        // Control track ID
         );

        fSDPLines[sdpLinesSize - 1] = '\0';
    }
    return fSDPLines;
}

RTPSink* BackchannelServerMediaSubsession::createNewRTPSink(Groupsock* rtpGroupsock,
                                                          unsigned char rtpPayloadTypeIfDynamic,
                                                          FramedSource* /*inputSource*/) {
    LOG_DEBUG("createNewRTPSink called for recvonly stream - returning NULL");
    // For a receive-only stream (a=recvonly in SDP), the server does not create a sink.
    // The framework handles creating the RTPSource when the client connects.
    return NULL;
}

FramedSource* BackchannelServerMediaSubsession::createNewStreamSource(unsigned /*clientSessionId*/,
                                                                    unsigned& estBitrate) {
    LOG_DEBUG("createNewStreamSource called");
    estBitrate = 0;
    return NULL;
}

void BackchannelServerMediaSubsession::startStream(unsigned clientSessionId, void* streamToken,
                                                 TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                                                 unsigned short& rtpSeqNum, unsigned& rtpTimestamp,
                                                 ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                                                 void* serverRequestAlternativeByteHandlerClientData) {
    LOG_INFO("startStream called for client session ID " << clientSessionId);

    OnDemandServerMediaSubsession::startStream(clientSessionId, streamToken,
                                               rtcpRRHandler, rtcpRRHandlerClientData,
                                               rtpSeqNum, rtpTimestamp,
                                               serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);

    RTPSource* rtpSource = (RTPSource*)streamToken;
    if (rtpSource == nullptr) {
        LOG_ERROR("RTPSource (streamToken) is NULL after base startStream call!");
         return;
     }
     LOG_DEBUG("Using RTPSource (streamToken) obtained from base class.");

     if (fMySink) { // Use fMySink
         fMySink->addSource(rtpSource); // Use fMySink
         LOG_INFO("Added RTPSource to BackchannelSink for client session " << clientSessionId); // Updated log
     } else {
         LOG_ERROR("fMySink is NULL in startStream!"); // Use fMySink
     }

     // LOG_INFO("Backchannel setup complete via startStream for client session " << clientSessionId); // Redundant?
 }

void BackchannelServerMediaSubsession::deleteStream(unsigned clientSessionId, void*& streamToken) {
    LOG_INFO("deleteStream called for client session ID " << clientSessionId);

     RTPSource* rtpSource = (RTPSource*)streamToken;

     if (fMySink && rtpSource) { // Use fMySink
         fMySink->removeSource(rtpSource); // Use fMySink
         LOG_INFO("Removed RTPSource from BackchannelSink for client session " << clientSessionId); // Updated log
     } else {
          LOG_WARN("fMySink or rtpSource is NULL in deleteStream for client session " << clientSessionId); // Use fMySink
     }

     OnDemandServerMediaSubsession::deleteStream(clientSessionId, streamToken);
 }
