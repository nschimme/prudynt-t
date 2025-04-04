#ifndef IMP_BACKCHANNEL_HPP
#define IMP_BACKCHANNEL_HPP

#include <imp/imp_audio.h>
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <vector>
#include <map>
#include <cstdint>

#include "Logger.hpp"
// #include "BackchannelSourceSelector.hpp" // Renamed/Handled by createNewSubsession
// #include "BackchannelPayloadExtractor.hpp" // Removed
// #include "BackchannelAudioTranscoder.hpp" // Removed
// #include "BackchannelPipeSink.hpp" // Removed
#include "BackchannelSink.hpp" // Include the renamed sink/source

// Forward declare subsession class
class BackchannelServerMediaSubsession;

class IMPBackchannel {
public:
    // Static factory method for creating the subsession and its associated Sink
    static BackchannelServerMediaSubsession* createNewSubsession(UsageEnvironment& env);

    // Static methods for managing global ADEC resources (Renamed)
    static int init();
    static void deinit();

    // Static method for looking up the ADEC channel ID for a given payload type
    // Returns -1 if not found or not initialized
    static int getADECChannel(IMPAudioPalyloadType payloadType);

private:
    // Static members for ADEC state (No change needed here)
    static std::map<IMPAudioPalyloadType, int> adecChannels; // Map payload type to ADEC channel ID
    static bool decoderInitialized;

    // Private constructor/destructor to prevent instantiation
    IMPBackchannel() = delete;
    ~IMPBackchannel() = delete;
    IMPBackchannel(const IMPBackchannel&) = delete;
    IMPBackchannel& operator=(const IMPBackchannel&) = delete;
};

#endif // IMP_BACKCHANNEL_HPP
