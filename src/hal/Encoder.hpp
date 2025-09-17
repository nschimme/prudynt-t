#pragma once

#include <vector>
#include <cstdint>
#include <sys/time.h>

// A generic structure for a single encoded frame (e.g., a NAL unit)
struct EncodedFrame {
    std::vector<uint8_t> data;
    timeval timestamp;
    bool is_key_frame;
};

// A generic structure for a stream of encoded data, which may contain multiple frames
struct EncodedStream {
    std::vector<EncodedFrame> frames;
};

class Encoder {
public:
    virtual ~Encoder() = default;

    // Initializes the encoder
    virtual bool init() = 0;

    // Deinitializes the encoder
    virtual void deinit() = 0;

    // Starts receiving pictures
    virtual bool start() = 0;

    // Stops receiving pictures
    virtual bool stop() = 0;

    // Polls for a new stream of data
    virtual int poll_stream(int timeout_ms) = 0;

    // Gets the next encoded stream
    virtual EncodedStream get_stream() = 0;

    // Releases the stream buffer
    virtual int release_stream() = 0;

    // Requests an IDR (Instantaneous Decoder Refresh) frame
    virtual int request_idr() = 0;
};
