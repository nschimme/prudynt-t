#ifndef BACKCHANNEL_PROCESSOR_HPP
#define BACKCHANNEL_PROCESSOR_HPP

#include <cstdio> // For FILE*
#include <vector>
#include <cstdint>
#include "globals.hpp" // For backchannel_stream definition

// Forward declare IMPBackchannel if needed for static methods?
// No, IMPBackchannel.hpp includes imp_audio.h which is needed here too.
#include "IMPBackchannel.hpp" // For static getADECChannel

class BackchannelProcessor {
public:
    // Constructor takes the shared stream state
    BackchannelProcessor(backchannel_stream* stream_data);
    ~BackchannelProcessor();

    // Main processing loop method
    void run();

private:
    // Helper to map RTP payload type to IMP payload type
    IMPAudioPalyloadType mapRtpToImpPayloadType(uint8_t rtpPayloadType);

    // Pipe management helpers
    bool initPipe();
    void closePipe();

    backchannel_stream* fStream; // Pointer to shared stream data (queue, running flag)
    FILE* fPipe;                 // Pipe to the external process (e.g., /bin/iac -s)

    // Prevent copying
    BackchannelProcessor(const BackchannelProcessor&) = delete;
    BackchannelProcessor& operator=(const BackchannelProcessor&) = delete;
};

#endif // BACKCHANNEL_PROCESSOR_HPP
