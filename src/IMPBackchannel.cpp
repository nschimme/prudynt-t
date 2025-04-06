#include "IMPBackchannel.hpp"

#include <liveMedia.hh>
#include <imp/imp_audio.h>
#include "Logger.hpp"

#define MODULE "IMPBackchannel"

IMPBackchannel* IMPBackchannel::createNew() {
    return new IMPBackchannel();
}

IMPBackchannel::IMPBackchannel() {
    init();
}

IMPBackchannel::~IMPBackchannel() {
    deinit();
}

int IMPBackchannel::init() {
    LOG_DEBUG("IMPBackchannel::init()");
    int ret = 0;

    IMPAudioDecChnAttr adec_attr;
    adec_attr.bufSize = 20;
    adec_attr.mode = ADEC_MODE_PACK;

    adec_attr.type = PT_G711U;
    int adChn = (int)IMPBackchannelFormat::PCMU;
    ret = IMP_ADEC_CreateChn(adChn, &adec_attr);
    LOG_DEBUG_OR_ERROR(ret, "IMP_ADEC_CreateChn(" << adChn << ", &encattr)");

    adec_attr.type = PT_G711A;
    adChn = (int)IMPBackchannelFormat::PCMA;
    ret = IMP_ADEC_CreateChn(adChn, &adec_attr);
    LOG_DEBUG_OR_ERROR(ret, "IMP_ADEC_CreateChn(" << adChn << ", &encattr)");

    return 0;
}

void IMPBackchannel::deinit() {
    LOG_DEBUG("IMPBackchannel::deinit()");
    int ret;

    #define DESTROY_ADEC(EnumName, NameString, PayloadType, Frequency, MimeType) \
        { \
            int adChn = (int)IMPBackchannelFormat::EnumName; \
            ret = IMP_ADEC_DestroyChn(adChn); \
            LOG_DEBUG_OR_ERROR(ret, "IMP_ADEC_DestroyChn(" << adChn << ")"); \
        }
    X_FOREACH_BACKCHANNEL_FORMAT(DESTROY_ADEC)
    #undef DESTROY_ADEC
}
