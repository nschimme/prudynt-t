#include "IMPBackchannel.hpp"
#include "BackchannelServerMediaSubsession.hpp" // Need full definition
#include "BackchannelSink.hpp" // Need full definition
#include "globals.hpp" // Need global_backchannel
#include <cstring> // For memcpy

#define MODULE "IMPBackchannel"

// Define static members
// Define static members
std::map<IMPAudioPalyloadType, int> IMPBackchannel::adecChannels;
bool IMPBackchannel::decoderInitialized = false;

int IMPBackchannel::init() { // Renamed from initADEC
    if (decoderInitialized) {
        LOG_WARN("IMPBackchannel ADEC already initialized."); // Updated log
        return 0; // Already initialized
    }
    LOG_INFO("Initializing IMPBackchannel ADEC resources..."); // Updated log
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
        LOG_ERROR("Failed to initialize any IMPBackchannel ADEC channels."); // Updated log
        deinit(); // Clean up any partially created channels
        return -1;
    }

    decoderInitialized = true;
    LOG_INFO("IMPBackchannel ADEC resources initialized successfully."); // Updated log
    return 0;
}

void IMPBackchannel::deinit() { // Renamed from deinitADEC
    if (!decoderInitialized) {
        // LOG_DEBUG("IMPBackchannel ADEC not initialized or already deinitialized.");
        return;
    }
    LOG_INFO("Deinitializing IMPBackchannel ADEC resources..."); // Updated log
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
    LOG_INFO("IMPBackchannel ADEC resources deinitialized."); // Updated log
}

int IMPBackchannel::getADECChannel(IMPAudioPalyloadType payloadType) {
     if (!decoderInitialized) {
        // This might be called before init or after deinit, don't log error?
        // LOG_ERROR("Cannot get ADEC channel: Decoder not initialized.");
        return -1;
    }
    auto it = adecChannels.find(payloadType);
    if (it == adecChannels.end()) {
        // LOG_WARN("No decoder channel configured for payload type: " << payloadType);
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

    // Create the Sink (which is also a FramedSource), passing the backchannel_stream pointer
    BackchannelSink* sink = BackchannelSink::createNew(env, global_backchannel.get()); // Pass the raw pointer to the struct
    if (!sink) {
        LOG_ERROR("Failed to create BackchannelSink!");
        return nullptr;
    }

    // Create the subsession, passing it the Sink
    BackchannelServerMediaSubsession* subsession = BackchannelServerMediaSubsession::createNew(env, sink);
    if (!subsession) {
        LOG_ERROR("Failed to create BackchannelServerMediaSubsession!");
        Medium::close(sink); // Clean up the created sink
        return nullptr;
    }

    LOG_INFO("Backchannel subsession and sink created successfully.");
    return subsession; // Success!
}

// --- decodeFrame method removed, logic moved to BackchannelProcessor ---
