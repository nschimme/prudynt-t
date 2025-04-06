#ifndef BACKCHANNEL_PROCESSOR_HPP
#define BACKCHANNEL_PROCESSOR_HPP

#include <cstdint>
#include <cstdio>
#include <vector>
#include <chrono>
#include "globals.hpp"
#include "IMPBackchannel.hpp"

class BackchannelProcessor {
public:
    BackchannelProcessor();
    ~BackchannelProcessor();

    void run();

private:
    IMPAudioPalyloadType mapFormatToImpPayloadType(IMPBackchannelFormat format);

    static std::vector<int16_t> resampleLinear(const std::vector<int16_t>& input_pcm, int input_rate, int output_rate);

    bool initPipe();
    void closePipe();

    bool handleIdleState();
    bool handleActiveState();

    bool processFrame(const BackchannelFrame& frame);
    bool decodeFrame(const uint8_t* payload, size_t payloadSize, IMPBackchannelFormat format, std::vector<int16_t>& outPcmBuffer, int& outSampleRate);
    bool writePcmToPipe(const std::vector<int16_t>& pcmBuffer);

    FILE* fPipe;
    int fPipeFd;
    std::chrono::steady_clock::time_point fLastPipeFullLogTime;

    BackchannelProcessor(const BackchannelProcessor&) = delete;
    BackchannelProcessor& operator=(const BackchannelProcessor&) = delete;
};

#endif // BACKCHANNEL_PROCESSOR_HPP
