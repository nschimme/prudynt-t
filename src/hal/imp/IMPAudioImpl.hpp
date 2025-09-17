#pragma once

#include "hal/Audio.hpp"
#include "Config.hpp"
#include <imp/imp_audio.h>
#include <stdexcept>

class IMPAudioImpl : public Audio {
public:
    IMPAudioImpl(int devId, int inChn, int aeChn);
    ~IMPAudioImpl() override;

    bool init() override;
    void deinit() override;

    int poll_frame(int timeout_ms) override;
    AudioFrame get_frame() override;
    int release_frame(AudioFrame& frame) override;

    bool supports_encoding() override;
    AudioFrame encode_frame(AudioFrame& frame) override;

    int get_samplerate() override;
    int get_bitwidth() override;
    int get_soundmode() override;

private:
    int devId;
    int inChn;
    int aeChn;

    IMPAudioIOAttr io_attr;
    IMPAudioFrame imp_frame;
    bool frame_active = false;

    bool enabledAgc = false;
    bool enabledHpf = false;
    bool enabledNs = false;
    int handle = 0;
};
