#ifndef BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP
#define BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP

#include "OnDemandServerMediaSubsession.hh"
#include "BasicUsageEnvironment.hh"
#include "RTPInterface.hh" // For RTPSink definition

// Forward declaration
class BackchannelSink;

// Need the track ID defined in CustomRTSPClientSession.cpp
extern const char* BACKCHANNEL_TRACK_ID;

class BackchannelServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static BackchannelServerMediaSubsession* createNew(UsageEnvironment& env);

    // Destructor should be virtual if inheriting
    virtual ~BackchannelServerMediaSubsession();

protected: // Called only by createNew()
    BackchannelServerMediaSubsession(UsageEnvironment& env);

    // Implement virtual functions from OnDemandServerMediaSubsession:
    // Used to generate the SDP description for this stream
    virtual char const* sdpLines();
    // Used to create the RTP sink when a client sets up this stream
    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
                                      unsigned char rtpPayloadTypeIfDynamic,
                                      FramedSource* inputSource);
    // We don't have an input source for receiving data, so this might not be needed
    // or should return NULL. Let's override it anyway.
    virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
                                                unsigned& estBitrate);

private:
    char* fSDPLines; // Buffer to hold generated SDP lines
};

#endif // BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP
