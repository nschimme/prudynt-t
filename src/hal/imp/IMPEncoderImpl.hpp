#pragma once

#include "hal/Encoder.hpp"
#include "Config.hpp"
#include "OSD.hpp"
#include <imp/imp_encoder.h>
#include <imp/imp_system.h>

#if defined(PLATFORM_T31) || defined(PLATFORM_C100) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
#define IMPEncoderCHNAttr IMPEncoderChnAttr
#define IMPEncoderCHNStat IMPEncoderChnStat
#endif

class IMPEncoderImpl : public Encoder {
public:
    IMPEncoderImpl(_stream *stream, int encChn, int encGrp, const char *name);
    ~IMPEncoderImpl() override;

    bool init() override;
    void deinit() override;
    bool start() override;
    bool stop() override;
    int poll_stream(int timeout_ms) override;
    EncodedStream get_stream() override;
    int release_stream() override;
    int request_idr() override;

private:
    void init_profile();

    _stream *stream;
    int encChn;
    int encGrp;
    const char *name;

    IMPEncoderCHNAttr chnAttr{};
    IMPCell fs{};
    IMPCell enc{};
    IMPCell osd_cell{};
    OSD *osd = nullptr;

    // The stream returned by the IMP API
    IMPEncoderStream imp_stream;
    bool stream_active = false;
};
