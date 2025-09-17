#pragma once

#include <vector>
#include <cstdint>
#include <sys/time.h>

// A generic enum for audio formats
enum class AudioFormat {
    PCM,
    G711A,
    G711U,
    G726,
    OPUS,
    AAC
};

// A generic structure for an audio frame
struct AudioFrame {
    std::vector<uint8_t> data;
    timeval timestamp;
    int bitwidth;
    int soundmode;
};

class Audio {
public:
    virtual ~Audio() = default;

    virtual bool init() = 0;
    virtual void deinit() = 0;

    virtual int poll_frame(int timeout_ms) = 0;
    virtual AudioFrame get_frame() = 0;
    virtual int release_frame(AudioFrame& frame) = 0;

    virtual bool supports_encoding() = 0;
    virtual AudioFrame encode_frame(AudioFrame& frame) = 0;

    virtual AudioFormat get_format() = 0;
    virtual int get_samplerate() = 0;
    virtual int get_bitwidth() = 0;
    virtual int get_soundmode() = 0;
    virtual int get_output_channel_count() = 0;
};
