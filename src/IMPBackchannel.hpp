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

class BackchannelServerMediaSubsession;

class IMPBackchannel {
public:
    static BackchannelServerMediaSubsession* createNewSubsession(UsageEnvironment& env);

    static int init();
    static void deinit();

    static int getADECChannel(IMPBackchannelFormat format);

    static unsigned getFrequency(IMPBackchannelFormat format);
    static IMPBackchannelFormat formatFromRtpPayloadType(unsigned char rtpPayloadType);
    static int rtpPayloadTypeFromFormat(IMPBackchannelFormat format);


private:
    static std::map<IMPBackchannelFormat, int> adecChannels;
    static bool decoderInitialized;

    IMPBackchannel() = delete;
    ~IMPBackchannel() = delete;
    IMPBackchannel(const IMPBackchannel&) = delete;
    IMPBackchannel& operator=(const IMPBackchannel&) = delete;
};

#endif // IMP_BACKCHANNEL_HPP
