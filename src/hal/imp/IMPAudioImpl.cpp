#include "IMPAudioImpl.hpp"
#include "Logger.hpp"
#include "AACEncoder.hpp"
#include "Opus.hpp"
#include <thread>

#define MODULE "IMPAudioImpl"

// This interface and the static functions are part of the private implementation
// details needed to register custom encoders with the IMP framework.
class IMPAudioEncoder
{
public:
    virtual int open() = 0;
    virtual int encode(IMPAudioFrame* data, unsigned char* outbuf, int* outLen) = 0;
    virtual int close() = 0;
    virtual ~IMPAudioEncoder() = default;
};

static thread_local IMPAudioEncoder *encoder = nullptr;

static int openEncoder(void* attr, void* enc)
{
    return encoder ? encoder->open() : -1;
}

static int encodeFrame(void* enc, IMPAudioFrame* data, unsigned char* outbuf, int* outLen)
{
    return encoder ? encoder->encode(data, outbuf, outLen) : -1;
}

static int closeEncoder(void* enc)
{
    return encoder ? encoder->close() : -1;
}

IMPAudioImpl::IMPAudioImpl(int devId, int inChn, int aeChn)
    : devId(devId), inChn(inChn), aeChn(aeChn) {
    LOG_DEBUG("IMPAudioImpl created for device " << devId);
}

IMPAudioImpl::~IMPAudioImpl() {
    deinit();
}

bool IMPAudioImpl::init()
{
    LOG_DEBUG("IMPAudioImpl::init()");
    int ret;

    IMPAudioEncChnAttr encattr = {
        .type = IMPAudioPalyloadType::PT_PCM,
        .bufSize = 20,
        .value = 0
    };
    float frameDuration = 0.040;
    int output_channel_count = cfg->audio.force_stereo ? 2 : 1;

    io_attr = {
        .samplerate = static_cast<IMPAudioSampleRate>(cfg->audio.input_sample_rate),
        .bitwidth = AUDIO_BIT_WIDTH_16,
        .soundmode = AUDIO_SOUND_MODE_MONO, // Input is always mono from hardware
        .frmNum = 30,
        .numPerFrm = 0,
        .chnCnt = 1
    };

    if (strcmp(cfg->audio.input_format, "OPUS") == 0) {
        format = AudioFormat::OPUS;
        frameDuration = 0.020f;
        encoder = Opus::createNew(io_attr.samplerate, output_channel_count);
    } else if (strcmp(cfg->audio.input_format, "AAC") == 0) {
        format = AudioFormat::AAC;
        encoder = AACEncoder::createNew(io_attr.samplerate, output_channel_count);
    } else if (strcmp(cfg->audio.input_format, "G711A") == 0) {
        format = AudioFormat::G711A;
        encattr.type = IMPAudioPalyloadType::PT_G711A;
        io_attr.samplerate = AUDIO_SAMPLE_RATE_8000;
    } else if (strcmp(cfg->audio.input_format, "G711U") == 0) {
        format = AudioFormat::G711U;
        encattr.type = IMPAudioPalyloadType::PT_G711U;
        io_attr.samplerate = AUDIO_SAMPLE_RATE_8000;
    } else if (strcmp(cfg->audio.input_format, "G726") == 0) {
        format = AudioFormat::G726;
        encattr.type = IMPAudioPalyloadType::PT_G726;
        io_attr.samplerate = AUDIO_SAMPLE_RATE_8000;
    } else {
        format = AudioFormat::PCM;
    }

    io_attr.numPerFrm = (int)io_attr.samplerate * frameDuration;

    if (encoder) {
        IMPAudioEncEncoder enc;
        enc.maxFrmLen = 1024;
        std::snprintf(enc.name, sizeof(enc.name), "%s", cfg->audio.input_format);
        enc.openEncoder = openEncoder;
        enc.encoderFrm = encodeFrame;
        enc.closeEncoder = closeEncoder;
        ret = IMP_AENC_RegisterEncoder(&handle, &enc);
        LOG_DEBUG_OR_ERROR(ret, "IMP_AENC_RegisterEncoder failed");
        encattr.type = static_cast<IMPAudioPalyloadType>(handle);
    }

    if (encattr.type > IMPAudioPalyloadType::PT_PCM) {
        ret = IMP_AENC_CreateChn(aeChn, &encattr);
        LOG_DEBUG_OR_ERROR(ret, "IMP_AENC_CreateChn failed");
    }

    ret = IMP_AI_SetPubAttr(devId, &io_attr);
    LOG_DEBUG_OR_ERROR(ret, "IMP_AI_SetPubAttr failed");
    if (ret != 0) return false;

    ret = IMP_AI_Enable(devId);
    LOG_DEBUG_OR_ERROR(ret, "IMP_AI_Enable failed");
    if (ret != 0) return false;

    IMPAudioIChnParam chnParam = {.usrFrmDepth = 30, .Rev = 0};
    ret = IMP_AI_SetChnParam(devId, inChn, &chnParam);
    LOG_DEBUG_OR_ERROR(ret, "IMP_AI_SetChnParam failed");
    if (ret != 0) return false;

    ret = IMP_AI_EnableChn(devId, inChn);
    LOG_DEBUG_OR_ERROR(ret, "IMP_AI_EnableChn failed");
    if (ret != 0) return false;

    ret = IMP_AI_SetVol(devId, inChn, cfg->audio.input_vol);
    LOG_DEBUG_OR_ERROR(ret, "IMP_AI_SetVol failed");

    if(cfg->audio.input_gain >= 0) {
        ret = IMP_AI_SetGain(devId, inChn, cfg->audio.input_gain);
        LOG_DEBUG_OR_ERROR(ret, "IMP_AI_SetGain failed");
    }

    return true;
}

void IMPAudioImpl::deinit() {
    LOG_DEBUG("IMPAudioImpl::deinit()");
    IMP_AI_DisableChn(devId, inChn);
    IMP_AI_Disable(devId);
}

int IMPAudioImpl::poll_frame(int timeout_ms) {
    return IMP_AI_PollingFrame(devId, inChn, timeout_ms);
}

AudioFrame IMPAudioImpl::get_frame() {
    AudioFrame result_frame;
    int ret = IMP_AI_GetFrame(devId, inChn, &imp_frame, IMPBlock::BLOCK);
    if (ret != 0) {
        LOG_ERROR("IMP_AI_GetFrame failed: " << ret);
        return result_frame;
    }
    frame_active = true;

    result_frame.data.assign((uint8_t*)imp_frame.virAddr, (uint8_t*)imp_frame.virAddr + imp_frame.len);
    result_frame.timestamp.tv_sec = imp_frame.timeStamp / 1000000;
    result_frame.timestamp.tv_usec = imp_frame.timeStamp % 1000000;
    result_frame.bitwidth = imp_frame.bitwidth;
    result_frame.soundmode = imp_frame.soundmode;

    return result_frame;
}

int IMPAudioImpl::release_frame(AudioFrame& frame) {
    if (frame_active) {
        frame_active = false;
        // The IMP frame is a member, so we pass its address
        return IMP_AI_ReleaseFrame(devId, inChn, &imp_frame);
    }
    return 0;
}

bool IMPAudioImpl::supports_encoding() {
    // A full implementation would check the configured format.
    return strcmp(cfg->audio.input_format, "PCM") != 0;
}

AudioFrame IMPAudioImpl::encode_frame(AudioFrame& frame) {
    // This is a simplified version. A full implementation would use the
    // IMP_AENC_* functions and the registered encoder.
    AudioFrame encoded_frame;
    if (!supports_encoding()) {
        return frame; // Return original frame if no encoding is needed
    }

    IMPAudioFrame input_imp_frame;
    input_imp_frame.virAddr = (uint32_t*)frame.data.data();
    input_imp_frame.len = frame.data.size();
    // ... set other fields

    IMP_AENC_SendFrame(aeChn, &input_imp_frame);

    IMPAudioStream imp_stream;
    if (IMP_AENC_GetStream(aeChn, &imp_stream, IMPBlock::BLOCK) == 0) {
        encoded_frame.data.assign(imp_stream.stream, imp_stream.stream + imp_stream.len);
        IMP_AENC_ReleaseStream(aeChn, &imp_stream);
    }

    return encoded_frame;
}

int IMPAudioImpl::get_samplerate() {
    return io_attr.samplerate;
}

int IMPAudioImpl::get_bitwidth() {
    return io_attr.bitwidth;
}

int IMPAudioImpl::get_soundmode() {
    return io_attr.soundmode;
}

int IMPAudioImpl::get_output_channel_count() {
    return io_attr.chnCnt;
}

AudioFormat IMPAudioImpl::get_format() {
    return format;
}
