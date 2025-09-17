#pragma once

#include "hal/Audio.hpp"

class V4LAudioImpl : public Audio {
public:
    V4LAudioImpl() = default;
    ~V4LAudioImpl() override = default;

    bool init() override { return false; }
    void deinit() override {}
    int poll_frame(int timeout_ms) override { return -1; }
    AudioFrame get_frame() override { return AudioFrame(); }
    int release_frame(AudioFrame& frame) override { return 0; }
    bool supports_encoding() override { return false; }
    AudioFrame encode_frame(AudioFrame& frame) override { return frame; }
    int get_samplerate() override { return 0; }
    int get_bitwidth() override { return 0; }
    int get_soundmode() override { return 0; }
};
