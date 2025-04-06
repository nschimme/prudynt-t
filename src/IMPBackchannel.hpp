#ifndef IMP_BACKCHANNEL_HPP
#define IMP_BACKCHANNEL_HPP

#include <imp/imp_audio.h>
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <vector>
#include <map>
#include <cstdint>

#include "Logger.hpp"

enum class IMPBackchannelFormat {
    UNKNOWN = -1,
    PCMU = 0,
    PCMA = 1
};

const unsigned IMP_BACKCHANNEL_FREQ_PCMU = 8000;
const unsigned IMP_BACKCHANNEL_FREQ_PCMA = 8000;

#include "BackchannelSink.hpp"

class IMPBackchannel {
public:
    static IMPBackchannel* createNew();
    IMPBackchannel();
    ~IMPBackchannel();

    int init();
    void deinit();

    int getADECChannel(IMPBackchannelFormat format);
    unsigned getFrequency(IMPBackchannelFormat format);
    IMPBackchannelFormat formatFromRtpPayloadType(unsigned char rtpPayloadType);
    int rtpPayloadTypeFromFormat(IMPBackchannelFormat format);

private:
    static std::map<IMPBackchannelFormat, int> adecChannels;
    static bool decoderInitialized;
};

#endif // IMP_BACKCHANNEL_HPP
