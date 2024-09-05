#include "OnDemandServerMediaSubsession.hh"

class BackchannelAudioServerMediaSubsession : public OnDemandServerMediaSubsession
{
public:
    static BackchannelAudioServerMediaSubsession* createNew(UsageEnvironment& env, const char* sdpDescription);
    
protected:
    BackchannelAudioServerMediaSubsession(UsageEnvironment& env, IMPAudioFormat audioFormat);
    virtual ~BackchannelAudioServerMediaSubsession();
    
    virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) override;
    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource) override;

private:
    int audioChn;
    std::map<unsigned, FramedSource*> clientBackchannelSources; // Mapping client session ID to their backchannel source
    std::mutex clientMapMutex; // Mutex to protect access to clientBackchannelSources

    // TODO: Implement SDP negotiation for each client based on their requested format.
};
