#ifndef IMP_BACKCHANNEL_HPP
#define IMP_BACKCHANNEL_HPP

#include <imp/imp_audio.h>
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <vector>
#include <map>
#include <cstdint>

#include "Logger.hpp"

// Define supported backchannel audio formats, aligned with ADEC channel IDs
enum class IMPBackchannelFormat {
    UNKNOWN = -1,
    PCMU = 0, // Corresponds to ADEC channel 0 (RTP Type 0)
    PCMA = 1  // Corresponds to ADEC channel 1 (RTP Type 8)
    // Add others like G726 if needed, assigning consecutive channel IDs
};

// Define frequencies for supported formats
const unsigned IMP_BACKCHANNEL_FREQ_PCMU = 8000;
const unsigned IMP_BACKCHANNEL_FREQ_PCMA = 8000;

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

    // Static method for looking up the ADEC channel ID for a given format
    // Returns -1 if not found or not initialized
    static int getADECChannel(IMPBackchannelFormat format); // Updated parameter type

    // --- Helper Functions ---
    // Get frequency for a given format
    static unsigned getFrequency(IMPBackchannelFormat format);
    // Map RTP Payload Type (0, 8, etc.) to format enum
    static IMPBackchannelFormat formatFromRtpPayloadType(unsigned char rtpPayloadType);
    // Map format enum back to RTP Payload Type
    static int rtpPayloadTypeFromFormat(IMPBackchannelFormat format);


private:
    // Static members for ADEC state
    static std::map<IMPBackchannelFormat, int> adecChannels; // Updated map key type
    static bool decoderInitialized;

    // Private constructor/destructor to prevent instantiation
    IMPBackchannel() = delete;
    ~IMPBackchannel() = delete;
    IMPBackchannel(const IMPBackchannel&) = delete;
    IMPBackchannel& operator=(const IMPBackchannel&) = delete;
};

#endif // IMP_BACKCHANNEL_HPP
