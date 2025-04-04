// Project Headers
#include "BackchannelServerMediaSubsession.hpp"
#include "BackchannelSink.hpp" // Our custom sink
#include "Logger.hpp"   // For logging

// Live555 Headers
#include "BasicUsageEnvironment.hh"
#include "RTPSource.hh" // Needed for RTPSource type definition

// Standard C/C++ Headers
// (None needed directly in this file after cleanup)

#define MODULE "BackchannelSubsession" // Logger module name

BackchannelServerMediaSubsession* BackchannelServerMediaSubsession::createNew(UsageEnvironment& env, BackchannelSink* sink) {
    return new BackchannelServerMediaSubsession(env, sink);
}

BackchannelServerMediaSubsession::BackchannelServerMediaSubsession(UsageEnvironment& env, BackchannelSink* sink)
    : OnDemandServerMediaSubsession(env, True), fSDPLines(nullptr), fMySink(sink) {
    LOG_DEBUG("BackchannelServerMediaSubsession created");
}

BackchannelServerMediaSubsession::~BackchannelServerMediaSubsession() {
    LOG_DEBUG("BackchannelServerMediaSubsession destroyed");
    delete[] fSDPLines;
}

// Generate the SDP description for this subsession
char const* BackchannelServerMediaSubsession::sdpLines() {
    if (fSDPLines == nullptr) {
        LOG_DEBUG("Generating SDP lines for backchannel");

        unsigned int sdpLinesSize = 200;
        fSDPLines = new char[sdpLinesSize];
        if (fSDPLines == nullptr) return nullptr;

        unsigned char rtpPayloadType = 8;

        snprintf(fSDPLines, sdpLinesSize,
                 "m=audio 0 RTP/AVP %d\r\n"
                 "c=IN IP4 0.0.0.0\r\n"
                 "b=AS:64\r\n"
                 "a=rtpmap:%d PCMA/8000\r\n"
                 "a=control:%s\r\n"
                 "a=recvonly\r\n",
                 rtpPayloadType,
                 rtpPayloadType,
                 trackId()
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

    fMySink->addSource(rtpSource);

    LOG_INFO("Backchannel setup complete via startStream for client session " << clientSessionId);
}

void BackchannelServerMediaSubsession::deleteStream(unsigned clientSessionId, void*& streamToken) {
    LOG_INFO("deleteStream called for client session ID " << clientSessionId);

    RTPSource* rtpSource = (RTPSource*)streamToken;

    fMySink->removeSource(rtpSource);

    OnDemandServerMediaSubsession::deleteStream(clientSessionId, streamToken);
}
