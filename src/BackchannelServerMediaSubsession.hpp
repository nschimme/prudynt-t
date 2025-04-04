#ifndef BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP
#define BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP

// Live555 Headers
#include "OnDemandServerMediaSubsession.hh"
#include "BasicUsageEnvironment.hh"
#include "RTPInterface.hh" // For RTPSink definition (though not used directly)
#include "RTPSource.hh" // Needed for RTPSource type definition

// Project Headers
#include "BackchannelSink.hpp" // Include the actual sink definition

class BackchannelServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static BackchannelServerMediaSubsession* createNew(UsageEnvironment& env, BackchannelSink *sink);

    // Destructor should be virtual if inheriting
    virtual ~BackchannelServerMediaSubsession();

protected: // Called only by createNew()
    BackchannelServerMediaSubsession(UsageEnvironment& env, BackchannelSink *sink);

    // Implement virtual functions from OnDemandServerMediaSubsession:
    // Used to generate the SDP description for this stream
    virtual char const* sdpLines();
    // Used to create the RTP sink when a client sets up this stream
    // Note: For receiving, we don't create a sink, the framework creates a source.
    // This function might need to return NULL or a dummy sink if required by the base class.
    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
                                      unsigned char rtpPayloadTypeIfDynamic,
                                      FramedSource* inputSource) override; // Add override specifier
    // We don't have an input source *to send*, so this should return NULL.
    virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
                                                unsigned& estBitrate) override; // Add override specifier

    // Overridden virtual functions for stream control:
    virtual void startStream(unsigned clientSessionId, void* streamToken,
                             TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                             unsigned short& rtpSeqNum, unsigned& rtpTimestamp,
                             ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                             void* serverRequestAlternativeByteHandlerClientData) override;

    virtual void deleteStream(unsigned clientSessionId, void*& streamToken) override;


private:
    char* fSDPLines = nullptr; // Buffer to hold generated SDP lines, initialize to nullptr
    BackchannelSink* fMySink = nullptr; // Pointer to our custom sink instance
};

#endif // BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP
