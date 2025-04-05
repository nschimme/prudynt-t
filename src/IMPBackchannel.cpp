#include "IMPBackchannel.hpp"
#include "BackchannelServerMediaSubsession.hpp" // Need full definition
#include "BackchannelSink.hpp" // Need full definition
#include "globals.hpp" // Need global_backchannel
#include <cstring> // For memcpy

#define MODULE "IMPBackchannel"

// Define static members
std::map<IMPAudioPalyloadType, int> IMPBackchannel::adecChannels;
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
        adecChannels[PT_G711U] = adChn_u;
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
        adecChannels[PT_G711A] = adChn_a;
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

int IMPBackchannel::getADECChannel(IMPAudioPalyloadType payloadType) {
     if (!decoderInitialized) {
        // Decoder not initialized (e.g., called before init or after deinit).
        return -1;
    }
    auto it = adecChannels.find(payloadType);
    if (it == adecChannels.end()) {
        // No specific channel configured for this type.
        return -1; // Not found
    }
    return it->second; // Return channel ID
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
