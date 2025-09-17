#pragma once

#include "hal/Encoder.hpp"

class V4LEncoderImpl : public Encoder {
public:
    V4LEncoderImpl() = default;
    ~V4LEncoderImpl() override = default;

    bool init() override { return false; }
    void deinit() override {}
    bool start() override { return false; }
    bool stop() override { return false; }
    int poll_stream(int timeout_ms) override { return -1; }
    EncodedStream get_stream() override { return EncodedStream(); }
    int release_stream() override { return 0; }
    int request_idr() override { return 0; }
};
