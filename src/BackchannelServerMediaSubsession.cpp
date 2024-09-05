#include "BackchannelAudioServerMediaSubsession.hpp"

BackchannelAudioServerMediaSubsession* BackchannelAudioServerMediaSubsession::createNew(UsageEnvironment& env, const char* sdpDescription)
{
    // Parse the SDP to determine the audio format
    if (strstr(sdpDescription, "L16")) {
        audioFormat = IMPAudioFormat::PCM;
    } else if (strstr(sdpDescription, "MP4A-LATM")) {
        audioFormat = IMPAudioFormat::AAC;
    } else {
        audioFormat = IMPAudioFormat::Unknown;
    }
    return new BackchannelAudioServerMediaSubsession(env, audioFormat);
}

BackchannelAudioServerMediaSubsession::BackchannelAudioServerMediaSubsession(UsageEnvironment& env, IMPAudioFormat audioFormat)
    : OnDemandServerMediaSubsession(env, true), audioFormat(audioFormat)
{
}

BackchannelAudioServerMediaSubsession::~BackchannelAudioServerMediaSubsession()
{
    std::lock_guard<std::mutex> lock(clientMapMutex);
    for (auto& pair : clientRTPSources) {
        Medium::close(pair.second);
    }
    clientRTPSources.clear();
}

FramedSource* BackchannelAudioServerMediaSubsession::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate)
{
    std::lock_guard<std::mutex> lock(clientMapMutex);
    FramedSource* source = nullptr;

    switch (global_audio[audioChn]->imp_audio->format) {
    case IMPAudioFormat::PCM:
        source = PCMBackchannelSource::createNew(envir());
        break;
    case IMPAudioFormat::AAC:
        source = AACBackchannelSource::createNew(envir());
        break;
    // Add more cases if additional audio formats are supported
    default:
        break;
    }

    if (source) {
        clientBackchannelSources[clientSessionId] = source;
        estBitrate = global_audio[audioChn]->imp_audio->bitrate;
    }

    return source;    
}

RTPSink* BackchannelAudioServerMediaSubsession::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource) {
    const char* rtpPayloadFormatName;
    unsigned rtpTimestampFrequency;

    switch (audioFormat)
    {
    case IMPAudioFormat::PCM:
        rtpPayloadFormatName = "L16";
        rtpTimestampFrequency = 16000;
        break;
    case IMPAudioFormat::AAC:
        rtpPayloadFormatName = "MP4A-LATM";
        rtpTimestampFrequency = 16000;
        break;
    default:
        return nullptr; // Unsupported format
    }
    
    return SimpleRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic, rtpTimestampFrequency, "audio", rtpPayloadFormatName, 1);
}
