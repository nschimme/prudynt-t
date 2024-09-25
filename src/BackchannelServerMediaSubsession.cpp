#include "BackchannelAudioServerMediaSubsession.hpp"
#include "IADSink.hpp"

BackchannelAudioServerMediaSubsession* BackchannelAudioServerMediaSubsession::createNew(UsageEnvironment& env)
{
    return new BackchannelAudioServerMediaSubsession(env);
}

BackchannelAudioServerMediaSubsession::BackchannelAudioServerMediaSubsession(UsageEnvironment& env)
    : OnDemandServerMediaSubsession(env, true)
{}

BackchannelAudioServerMediaSubsession::~BackchannelAudioServerMediaSubsession()
{}

MediaSink* BackchannelAudioServerMediaSubsession::createNewStreamDestination(unsigned clientSessionId, unsigned& estBitrate)
{
    return IADSink::createNew(envir());
}

RTPSource* BackchannelAudioServerMediaSubsession::createNewRTPSource(Groupsock* rtpGroupsock,
                                                                     unsigned char rtpPayloadTypeIfDynamic,
                                                                     MediaSink* outputSink)
{
    // TODO: Negotiate per client
    const char* mimeTypeString = "audio/PCMU";
    const char* rtpPayloadFormatName = 0;
    unsigned rtpTimestampFrequency = 8000;

    return SimpleRTPSource::createNew(envir(), rtpGroupsock,
        rtpPayloadFormatName, rtpTimestampFrequency,
        mimeTypeString);
}

FramedSource* BackchannelAudioServerMediaSubsession::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate)
{
    return nullptr;
}

RTPSink* BackchannelAudioServerMediaSubsession::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource)
{
    return nullptr;
}
