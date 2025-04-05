#include "IMPBackchannel.hpp"
#include "BackchannelServerMediaSubsession.hpp" // Need full definition
#include "BackchannelSink.hpp" // Need full definition
#include "globals.hpp" // Need global_backchannel
#include <cstring> // For memcpy

#define MODULE "IMPBackchannel"

// Define static members (using updated types from header)
std::map<IMPBackchannelFormat, int> IMPBackchannel::adecChannels;
bool IMPBackchannel::decoderInitialized = false;

int IMPBackchannel::init() {
    if (decoderInitialized) {
        LOG_WARN("IMPBackchannel ADEC already initialized.");
        return 0; // Already initialized
    }
    LOG_INFO("Initializing IMPBackchannel ADEC resources...");
    int ret = 0;
    bool success = false;

    IMPAudioDecChnAttr adec_attr;
    adec_attr.bufSize = 20; // Adjust if needed
    adec_attr.mode = ADEC_MODE_PACK;

    // Create channel for G711U (PCMU) - Channel ID 0
    adec_attr.type = PT_G711U;
    int adChn_u = 0;
    ret = IMP_ADEC_CreateChn(adChn_u, &adec_attr);
    if (ret == 0) {
        LOG_INFO("Created global ADEC channel " << adChn_u << " for PT_G711U");
        adecChannels[IMPBackchannelFormat::PCMU] = adChn_u; // Use enum as key
        success = true;
    } else {
        LOG_ERROR("Failed to create global ADEC channel " << adChn_u << " for PT_G711U: " << ret);
    }

    // Create channel for G711A (PCMA) - Channel ID 1
    adec_attr.type = PT_G711A;
    int adChn_a = 1;
    ret = IMP_ADEC_CreateChn(adChn_a, &adec_attr);
     if (ret == 0) {
        LOG_INFO("Created global ADEC channel " << adChn_a << " for PT_G711A");
        adecChannels[IMPBackchannelFormat::PCMA] = adChn_a; // Use enum as key
        success = true;
    } else {
        LOG_ERROR("Failed to create global ADEC channel " << adChn_a << " for PT_G711A: " << ret);
    }

    // Add more channels here if needed

    if (!success) {
        LOG_ERROR("Failed to initialize any IMPBackchannel ADEC channels.");
        deinit(); // Clean up any partially created channels
        return -1;
    }

    decoderInitialized = true;
    LOG_INFO("IMPBackchannel ADEC resources initialized successfully.");
    return 0;
}

void IMPBackchannel::deinit() {
    if (!decoderInitialized) {
        // Not initialized or already deinitialized, nothing to do.
        return;
    }
    LOG_INFO("Deinitializing IMPBackchannel ADEC resources...");
    for (auto const& [payloadType, channelId] : adecChannels) {
        int ret = IMP_ADEC_DestroyChn(channelId);
        if (ret != 0) {
             LOG_ERROR("IMP_ADEC_DestroyChn(" << channelId << ") failed: " << ret);
        } else {
             LOG_DEBUG("IMP_ADEC_DestroyChn(" << channelId << ") succeeded.");
        }
    }
    adecChannels.clear();
    decoderInitialized = false;
    LOG_INFO("IMPBackchannel ADEC resources deinitialized.");
}

// Updated signature to match header (using IMPBackchannelFormat)
int IMPBackchannel::getADECChannel(IMPBackchannelFormat format) {
     if (!decoderInitialized) {
        // Decoder not initialized (e.g., called before init or after deinit).
        return -1;
    }
    auto it = adecChannels.find(format); // Use format enum for lookup
    if (it == adecChannels.end()) {
        // No specific channel configured for this type.
        LOG_WARN("No ADEC channel found for format: " << static_cast<int>(format));
        return -1; // Not found
    }
    return it->second; // Return channel ID
}

// --- Helper Function Implementations ---

unsigned IMPBackchannel::getFrequency(IMPBackchannelFormat format) {
    // Frequency is currently the same for both supported formats
    switch (format) {
        case IMPBackchannelFormat::PCMU: return IMP_BACKCHANNEL_FREQ_PCMU;
        case IMPBackchannelFormat::PCMA: return IMP_BACKCHANNEL_FREQ_PCMA;
        default:
            LOG_WARN("Requested frequency for unknown/unsupported format: " << static_cast<int>(format));
            return 8000; // Default to 8000Hz as a fallback
    }
}

IMPBackchannelFormat IMPBackchannel::formatFromRtpPayloadType(unsigned char rtpPayloadType) {
    // Map standard RTP payload types to our internal enum (which matches ADEC channel ID)
    switch (rtpPayloadType) {
        case 0: return IMPBackchannelFormat::PCMU; // RTP Type 0 -> Enum PCMU (0)
        case 8: return IMPBackchannelFormat::PCMA; // RTP Type 8 -> Enum PCMA (1)
        default:
            LOG_WARN("Mapping unknown RTP payload type to format: " << (int)rtpPayloadType);
            return IMPBackchannelFormat::UNKNOWN;
    }
}

int IMPBackchannel::rtpPayloadTypeFromFormat(IMPBackchannelFormat format) {
     // Map our internal enum (which matches ADEC channel ID) back to standard RTP payload types
     switch (format) {
        case IMPBackchannelFormat::PCMU: return 0; // Enum PCMU (0) -> RTP Type 0
        case IMPBackchannelFormat::PCMA: return 8; // Enum PCMA (1) -> RTP Type 8
        default:
            LOG_WARN("Mapping unknown format to RTP payload type: " << static_cast<int>(format));
            return -1; // Indicate error/unknown
    }
}


BackchannelServerMediaSubsession* IMPBackchannel::createNewSubsession(UsageEnvironment& env) {
    LOG_DEBUG("Creating backchannel subsession and sink...");

    // Check if global backchannel stream is initialized
    if (!global_backchannel) {
         LOG_ERROR("global_backchannel not initialized!");
         return nullptr;
     }

     // Sink is now created internally by BackchannelServerMediaSubsession::getStreamParameters
     // via createNewStreamDestination. We just need to create the subsession itself,
     // passing the necessary stream data pointer.

     // Create the subsession, passing the stream data pointer
     BackchannelServerMediaSubsession* subsession = BackchannelServerMediaSubsession::createNew(env, global_backchannel.get());
     if (!subsession) {
         LOG_ERROR("Failed to create BackchannelServerMediaSubsession!");
         // No sink to clean up here anymore
         return nullptr;
     }

     LOG_INFO("Backchannel subsession created successfully."); // Updated log message slightly
    return subsession;
}
