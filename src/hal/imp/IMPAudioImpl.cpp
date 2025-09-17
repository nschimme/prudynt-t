#include "IMPAudioImpl.hpp"
#include "Logger.hpp"

#define MODULE "IMPAudioImpl"

IMPAudioImpl::IMPAudioImpl(int devId, int inChn, int aeChn)
    : devId(devId), inChn(inChn), aeChn(aeChn) {
    LOG_DEBUG("IMPAudioImpl created for device " << devId);
}

IMPAudioImpl::~IMPAudioImpl() {
    deinit();
}

bool IMPAudioImpl::init() {
    LOG_DEBUG("IMPAudioImpl::init()");
    int ret;

    io_attr = {
        .samplerate = static_cast<IMPAudioSampleRate>(cfg->audio.input_sample_rate),
        .bitwidth = AUDIO_BIT_WIDTH_16,
        .soundmode = cfg->audio.force_stereo ? AUDIO_SOUND_MODE_STEREO : AUDIO_SOUND_MODE_MONO,
        .frmNum = 30,
        .numPerFrm = (int)(cfg->audio.input_sample_rate * 0.040f),
        .chnCnt = cfg->audio.force_stereo ? 2 : 1
    };

    ret = IMP_AI_SetPubAttr(devId, &io_attr);
    if (ret != 0) {
        LOG_ERROR("IMP_AI_SetPubAttr failed: " << ret);
        return false;
    }

    ret = IMP_AI_Enable(devId);
    if (ret != 0) {
        LOG_ERROR("IMP_AI_Enable failed: " << ret);
        return false;
    }

    IMPAudioIChnParam chnParam = {.usrFrmDepth = 30, .Rev = 0};
    ret = IMP_AI_SetChnParam(devId, inChn, &chnParam);
    if (ret != 0) {
        LOG_ERROR("IMP_AI_SetChnParam failed: " << ret);
        return false;
    }

    ret = IMP_AI_EnableChn(devId, inChn);
    if (ret != 0) {
        LOG_ERROR("IMP_AI_EnableChn failed: " << ret);
        return false;
    }

    // Additional setup for volume, gain, NS, HPF, AGC would go here...

    // Determine and store the audio format
    if (strcmp(cfg->audio.input_format, "OPUS") == 0) format = AudioFormat::OPUS;
    else if (strcmp(cfg->audio.input_format, "AAC") == 0) format = AudioFormat::AAC;
    else if (strcmp(cfg->audio.input_format, "G711A") == 0) format = AudioFormat::G711A;
    else if (strcmp(cfg->audio.input_format, "G711U") == 0) format = AudioFormat::G711U;
    else if (strcmp(cfg->audio.input_format, "G726") == 0) format = AudioFormat::G726;
    else format = AudioFormat::PCM;

    // A full implementation would also handle the encoder setup here.

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
