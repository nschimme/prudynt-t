#include "IMPBackchannel.hpp"
#include "globals.hpp"
#include <cstring>

#define MODULE "IMPBackchannel"

IMPBackchannel* IMPBackchannel::createNew() {
    return new IMPBackchannel();
}

std::map<IMPBackchannelFormat, int> IMPBackchannel::adecChannels;
bool IMPBackchannel::decoderInitialized = false;

IMPBackchannel::IMPBackchannel() {
    init();
}

IMPBackchannel::~IMPBackchannel() {
    deinit();
}

int IMPBackchannel::init() {
    if (decoderInitialized) {
        LOG_DEBUG("ADEC already initialized.");
        return 0;
    }
    LOG_INFO("Initializing ADEC resources...");
    int ret = 0;
    bool success = false;

    IMPAudioDecChnAttr adec_attr;
    adec_attr.bufSize = 20;
    adec_attr.mode = ADEC_MODE_PACK;

    adec_attr.type = PT_G711U;
    int adChn_u = 0;
    ret = IMP_ADEC_CreateChn(adChn_u, &adec_attr);
    if (ret == 0) {
        LOG_INFO("Created ADEC channel " << adChn_u << " for G711U");
        adecChannels[IMPBackchannelFormat::PCMU] = adChn_u;
        success = true;
    } else {
        LOG_ERROR("Failed to create global ADEC channel " << adChn_u << " for PT_G711U: " << ret);
    }

    adec_attr.type = PT_G711A;
    int adChn_a = 1;
    ret = IMP_ADEC_CreateChn(adChn_a, &adec_attr);
     if (ret == 0) {
        LOG_INFO("Created ADEC channel " << adChn_a << " for G711A");
        adecChannels[IMPBackchannelFormat::PCMA] = adChn_a;
        success = true;
    } else {
        LOG_ERROR("Failed to create global ADEC channel " << adChn_a << " for PT_G711A: " << ret);
    }


    if (!success) {
        LOG_ERROR("Failed to initialize any IMPBackchannel ADEC channels.");
        deinit();
        return -1;
    }

    decoderInitialized = true;
    LOG_INFO("ADEC resources initialized successfully.");
    return 0;
}

void IMPBackchannel::deinit() {
    if (!decoderInitialized) {
        return;
    }
    LOG_INFO("Deinitializing ADEC resources...");
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
    LOG_INFO("ADEC resources deinitialized.");
}

int IMPBackchannel::getADECChannel(IMPBackchannelFormat format) {
     if (!decoderInitialized) {
        return -1;
    }
    auto it = adecChannels.find(format);
    if (it == adecChannels.end()) {
        LOG_WARN("No ADEC channel found for format: " << static_cast<int>(format));
        return -1;
    }
    return it->second;
}

unsigned IMPBackchannel::getFrequency(IMPBackchannelFormat format) {
    switch (format) {
        case IMPBackchannelFormat::PCMU: return IMP_BACKCHANNEL_FREQ_PCMU;
        case IMPBackchannelFormat::PCMA: return IMP_BACKCHANNEL_FREQ_PCMA;
        default:
            LOG_WARN("Requested frequency for unknown format: " << static_cast<int>(format));
            return 8000;
    }
}

IMPBackchannelFormat IMPBackchannel::formatFromRtpPayloadType(unsigned char rtpPayloadType) {
    switch (rtpPayloadType) {
        case 0: return IMPBackchannelFormat::PCMU;
        case 8: return IMPBackchannelFormat::PCMA;
        default:
            LOG_WARN("Mapping unknown RTP payload type: " << (int)rtpPayloadType);
            return IMPBackchannelFormat::UNKNOWN;
    }
}

int IMPBackchannel::rtpPayloadTypeFromFormat(IMPBackchannelFormat format) {
     switch (format) {
        case IMPBackchannelFormat::PCMU: return 0;
        case IMPBackchannelFormat::PCMA: return 8;
        default:
            LOG_WARN("Mapping unknown format to RTP payload type: " << static_cast<int>(format));
            return -1;
    }
}
