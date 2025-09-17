#include "IMPEncoderImpl.hpp"
#include "Logger.hpp"

#define MODULE "IMPEncoderImpl"

#if defined(PLATFORM_T31) || defined(PLATFORM_C100) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
#define IMPEncoderCHNAttr IMPEncoderChnAttr
#define IMPEncoderCHNStat IMPEncoderChnStat
#endif

// Helper function to create quantization tables for JPEG
static void MakeTables(int q, uint8_t *lqt, uint8_t *cqt)
{
    static const std::array<int, 64> jpeg_luma_quantizer = {{16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99}};
    static const std::array<int, 64> jpeg_chroma_quantizer = {{17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99}};
    q = std::max(1, std::min(q, 99));
    if (q < 50) q = 5000 / q;
    else q = 200 - 2 * q;

    for (int i = 0; i < 64; ++i) {
        int lq = (jpeg_luma_quantizer[i] * q + 50) / 100;
        int cq = (jpeg_chroma_quantizer[i] * q + 50) / 100;
        lqt[i] = static_cast<uint8_t>(std::max(1, std::min(lq, 255)));
        cqt[i] = static_cast<uint8_t>(std::max(1, std::min(cq, 255)));
    }
}


IMPEncoderImpl::IMPEncoderImpl(_stream *stream, int encChn, int encGrp, const char *name)
    : stream(stream), encChn(encChn), encGrp(encGrp), name(name) {
    LOG_DEBUG("IMPEncoderImpl created for channel " << encChn);
}

IMPEncoderImpl::~IMPEncoderImpl() {
    LOG_DEBUG("IMPEncoderImpl destroyed for channel " << encChn);
    if (osd) {
        delete osd;
        osd = nullptr;
    }
}

bool IMPEncoderImpl::init() {
    LOG_DEBUG("IMPEncoderImpl::init(" << encChn << ", " << encGrp << ")");
    int ret = 0;

    init_profile();

    ret = IMP_Encoder_CreateChn(encChn, &chnAttr);
    if (ret < 0) {
        LOG_ERROR("IMP_Encoder_CreateChn(" << encChn << ") failed: " << ret);
        return false;
    }

    ret = IMP_Encoder_RegisterChn(encGrp, encChn);
    if (ret < 0) {
        LOG_ERROR("IMP_Encoder_RegisterChn(" << encGrp << ", " << encChn << ") failed: " << ret);
        return false;
    }

    if (strcmp(stream->format, "JPEG") != 0) {
        ret = IMP_Encoder_CreateGroup(encGrp);
        if (ret < 0) {
            LOG_ERROR("IMP_Encoder_CreateGroup(" << encGrp << ") failed: " << ret);
            return false;
        }

        fs = {DEV_ID_FS, encGrp, 0};
        enc = {DEV_ID_ENC, encGrp, 0};
        osd_cell = {DEV_ID_OSD, encGrp, 0};

        if (stream->osd.enabled) {
            osd = OSD::createNew(stream->osd, encGrp, encChn, name);
            IMP_System_Bind(&fs, &osd_cell);
            IMP_System_Bind(&osd_cell, &enc);
        } else {
            IMP_System_Bind(&fs, &enc);
        }
    }
    return true;
}

void IMPEncoderImpl::deinit() {
    LOG_DEBUG("IMPEncoderImpl::deinit(" << encChn << ", " << encGrp << ")");

    if (strcmp(stream->format, "JPEG") != 0) {
        if (osd) {
            IMP_System_UnBind(&fs, &osd_cell);
            IMP_System_UnBind(&osd_cell, &enc);
            osd->exit();
            delete osd;
            osd = nullptr;
        } else {
            IMP_System_UnBind(&fs, &enc);
        }
    }

    IMP_Encoder_UnRegisterChn(encChn);
    IMP_Encoder_DestroyChn(encChn);
    IMP_Encoder_DestroyGroup(encGrp);
}

bool IMPEncoderImpl::start() {
    int ret = IMP_Encoder_StartRecvPic(encChn);
    LOG_DEBUG_OR_ERROR(ret, "IMP_Encoder_StartRecvPic(" << encChn << ")");
    return ret == 0;
}

bool IMPEncoderImpl::stop() {
    int ret = IMP_Encoder_StopRecvPic(encChn);
    LOG_DEBUG_OR_ERROR(ret, "IMP_Encoder_StopRecvPic(" << encChn << ")");
    return ret == 0;
}

int IMPEncoderImpl::poll_stream(int timeout_ms) {
    return IMP_Encoder_PollingStream(encChn, timeout_ms);
}

EncodedStream IMPEncoderImpl::get_stream() {
    EncodedStream result_stream;
    int ret = IMP_Encoder_GetStream(encChn, &imp_stream, GET_STREAM_BLOCKING);
    if (ret != 0) {
        LOG_ERROR("IMP_Encoder_GetStream(" << encChn << ") failed");
        return result_stream;
    }
    stream_active = true;

    struct timeval monotonic_time;
    gettimeofday(&monotonic_time, NULL);

    for (uint32_t i = 0; i < imp_stream.packCount; ++i) {
        EncodedFrame frame;
        frame.timestamp = monotonic_time;
        frame.is_key_frame = false;

#if defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41) || defined(PLATFORM_C100)
        IMPEncoderPack *pack = &imp_stream.pack[i];
        if (pack->length) {
            uint8_t *data_ptr = (uint8_t *) imp_stream.virAddr;
            if (strcmp(stream->format, "JPEG") == 0) {
                uint32_t remSize = imp_stream.streamSize - pack->offset;
                if (remSize < pack->length) {
                    // Wraparound case
                    frame.data.insert(frame.data.end(), data_ptr + pack->offset, data_ptr + imp_stream.streamSize);
                    frame.data.insert(frame.data.end(), data_ptr, data_ptr + (pack->length - remSize));
                } else {
                    frame.data.insert(frame.data.end(), data_ptr + pack->offset, data_ptr + pack->offset + pack->length);
                }
            } else {
                frame.data.insert(frame.data.end(), data_ptr + pack->offset, data_ptr + pack->offset + pack->length);
            }
        }
#else
        uint8_t *start = (uint8_t *) imp_stream.pack[i].virAddr;
        uint8_t *end = start + imp_stream.pack[i].length;
        frame.data.insert(frame.data.end(), start, end);
#endif

        if (strcmp(stream->format, "H264") == 0 && frame.data.size() > 4) {
            frame.data.erase(frame.data.begin(), frame.data.begin() + 4);
        }

#if defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41) || defined(PLATFORM_C100)
        if (imp_stream.pack[i].nalType.h264NalType == 7 || imp_stream.pack[i].nalType.h264NalType == 8 || imp_stream.pack[i].nalType.h264NalType == 5) {
            frame.is_key_frame = true;
        } else if (imp_stream.pack[i].nalType.h265NalType == 32) {
            frame.is_key_frame = true;
        }
#else
        if (imp_stream.pack[i].dataType.h264Type == 7 || imp_stream.pack[i].dataType.h264Type == 8 || imp_stream.pack[i].dataType.h264Type == 5) {
            frame.is_key_frame = true;
        }
#if defined(PLATFORM_T30)
        else if (imp_stream.pack[i].dataType.h265Type == 32) {
            frame.is_key_frame = true;
        }
#endif
#endif
        result_stream.frames.push_back(frame);
    }
    return result_stream;
}

int IMPEncoderImpl::release_stream() {
    if (stream_active) {
        stream_active = false;
        return IMP_Encoder_ReleaseStream(encChn, &imp_stream);
    }
    return 0;
}

int IMPEncoderImpl::request_idr() {
    return IMP_Encoder_RequestIDR(encChn);
}

// This is a large and complex function that is highly platform-specific.
// It is moved here from the original IMPEncoder.cpp.
void IMPEncoderImpl::init_profile() {
    IMPEncoderRcAttr *rcAttr;
    memset(&chnAttr, 0, sizeof(IMPEncoderCHNAttr));
    rcAttr = &chnAttr.rcAttr;

#if defined(PLATFORM_T31) || defined(PLATFORM_C100) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
    IMPEncoderRcMode rcMode = IMP_ENC_RC_MODE_CAPPED_QUALITY;
    IMPEncoderProfile encoderProfile = IMP_ENC_PROFILE_AVC_HIGH;

    if (strcmp(stream->format, "H265") == 0) {
        encoderProfile = IMP_ENC_PROFILE_HEVC_MAIN;
    } else if (strcmp(stream->format, "JPEG") == 0) {
        encoderProfile = IMP_ENC_PROFILE_JPEG;
        IMP_Encoder_SetDefaultParam(&chnAttr, encoderProfile, IMP_ENC_RC_MODE_FIXQP,
                                    stream->width, stream->height, 24, 1, 0, 0, stream->jpeg_quality, 0);
        return;
    }

    if (strcmp(stream->mode, "FIXQP") == 0) rcMode = IMP_ENC_RC_MODE_FIXQP;
    else if (strcmp(stream->mode, "VBR") == 0) rcMode = IMP_ENC_RC_MODE_VBR;
    else if (strcmp(stream->mode, "CBR") == 0) rcMode = IMP_ENC_RC_MODE_CBR;
    else if (strcmp(stream->mode, "CAPPED_VBR") == 0) rcMode = IMP_ENC_RC_MODE_CAPPED_VBR;
    else if (strcmp(stream->mode, "CAPPED_QUALITY") == 0) rcMode = IMP_ENC_RC_MODE_CAPPED_QUALITY;

    IMP_Encoder_SetDefaultParam(&chnAttr, encoderProfile, rcMode, stream->width, stream->height, stream->fps, 1, stream->gop, 2, -1, stream->bitrate);

#else
    if (strcmp(stream->format, "JPEG") == 0) {
        chnAttr.encAttr.enType = PT_JPEG;
        chnAttr.encAttr.bufSize = 0;
        chnAttr.encAttr.profile = 2;
        chnAttr.encAttr.picWidth = stream->width;
        chnAttr.encAttr.picHeight = stream->height;
        return;
    }
    else if (strcmp(stream->format, "H264") == 0) chnAttr.encAttr.enType = PT_H264;
#if defined(PLATFORM_T30)
    else if (strcmp(stream->format, "H265") == 0) chnAttr.encAttr.enType = PT_H265;
#endif

    IMPEncoderRcMode rcMode = ENC_RC_MODE_SMART;
    if (strcmp(stream->mode, "FIXQP") == 0) rcMode = ENC_RC_MODE_FIXQP;
    else if (strcmp(stream->mode, "VBR") == 0) rcMode = ENC_RC_MODE_VBR;
    else if (strcmp(stream->mode, "CBR") == 0) rcMode = ENC_RC_MODE_CBR;

    chnAttr.encAttr.profile = stream->profile;
    chnAttr.encAttr.bufSize = 0;
    chnAttr.encAttr.picWidth = stream->width;
    chnAttr.encAttr.picHeight = stream->height;
    chnAttr.rcAttr.outFrmRate.frmRateNum = stream->fps;
    chnAttr.rcAttr.outFrmRate.frmRateDen = 1;
    rcAttr->maxGop = stream->max_gop;

    // ... (rest of the platform-specific RC settings)
#endif
}
