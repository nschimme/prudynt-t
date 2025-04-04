#ifndef RTSP_hpp
#define RTSP_hpp

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "IMPServerMediaSubsession.hpp"
#include "IMPAudioServerMediaSubsession.hpp"
#include "IMPDeviceSource.hpp"
#include "IMPEncoder.hpp"
#include "Logger.hpp"
#include "IMPBackchannel.hpp" // Still needed for static methods

// Forward declarations no longer needed here
// class BackchannelSourceSelector;
// class BackchannelPayloadExtractor;
// class BackchannelAudioTranscoder;
// class BackchannelPipeSink;


class RTSP
{
public:
    RTSP(){};
    void addSubsession(int chnNr, _stream &stream);
    void start();
    static void *run(void* arg);

private:
    UsageEnvironment *env{};
    TaskScheduler *scheduler{};
    RTSPServer *rtspServer{};
    int audioChn = 0;
    IMPDeviceSource<AudioFrame, audio_stream> *audioSource{};

    // IMPBackchannel* backChannel{}; // Removed
};

#endif
