#ifndef BACKCHANNEL_PROCESSOR_HPP
#define BACKCHANNEL_PROCESSOR_HPP

#include <cstdint>
#include <cstdio>
#include <vector>
#include <chrono> // For std::chrono::steady_clock
#include "globals.hpp"
#include "IMPBackchannel.hpp"

class BackchannelProcessor {
public:
    // Constructor takes the shared stream state
    BackchannelProcessor(backchannel_stream* stream_data);
    ~BackchannelProcessor();

    // Main processing loop method
    void run();

private:
    // Helper to map IMPBackchannelFormat enum to IMP payload type
    IMPAudioPalyloadType mapFormatToImpPayloadType(IMPBackchannelFormat format);

    // Simple linear resampling helper
    static std::vector<int16_t> resampleLinear(const std::vector<int16_t>& input_pcm, int input_rate, int output_rate);

    // Pipe management helpers
    bool initPipe();
    void closePipe();

    // Main loop state handlers
    bool handleIdleState();
    bool handleActiveState();

    // Frame processing pipeline (updated signatures)
    bool processFrame(const BackchannelFrame& frame);
    bool decodeFrame(const uint8_t* payload, size_t payloadSize, IMPBackchannelFormat format, std::vector<int16_t>& outPcmBuffer, int& outSampleRate);
    bool writePcmToPipe(const std::vector<int16_t>& pcmBuffer);

    backchannel_stream* fStream; // Pointer to shared stream data (queue, running flag)
    FILE* fPipe;                 // Pipe to the external process (e.g., /bin/iac -s)
    int fPipeFd;                 // File descriptor for non-blocking writes
    std::chrono::steady_clock::time_point fLastPipeFullLogTime; // For rate-limiting logs

    // Prevent copying
    BackchannelProcessor(const BackchannelProcessor&) = delete;
    BackchannelProcessor& operator=(const BackchannelProcessor&) = delete;
};

#endif // BACKCHANNEL_PROCESSOR_HPP
