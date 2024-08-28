#include "AACEncoder.hpp"
#include "Config.hpp"
#include <cstdint>
#include "Logger.hpp"

AACEncoder* AACEncoder::createNew(int sampleRate, int numChn)
{
    return new AACEncoder(sampleRate, numChn);
}

AACEncoder::AACEncoder(int sampleRate, int numChn) : sampleRate(sampleRate), numChn(numChn)
{
}

AACEncoder::~AACEncoder()
{
    close();
}

int AACEncoder::open()
{
    if (aacEncOpen(&handle, 0, numChn) != AACENC_OK)
    {
        LOG_ERROR("Failed to open AAC encoder.");
        return -1;
    }

    if (aacEncoder_SetParam(handle, AACENC_AOT, AOT_AAC_LC) != AACENC_OK ||
        aacEncoder_SetParam(handle, AACENC_SAMPLERATE, sampleRate) != AACENC_OK ||
        aacEncoder_SetParam(handle, AACENC_CHANNELMODE, numChn == 1 ? MODE_1 : MODE_2) != AACENC_OK ||
        aacEncoder_SetParam(handle, AACENC_BITRATE, cfg->audio.input_bitrate) != AACENC_OK ||
        aacEncoder_SetParam(handle, AACENC_TRANSMUX, TT_MP4_RAW) != AACENC_OK)
    {
        LOG_ERROR("Failed to set AAC encoder parameters.");
        return -1;
    }

    LOG_INFO("AAC Encoder initialized with bitrate: " << cfg->audio.input_bitrate);
    return 0;
}

int AACEncoder::close()
{
    if (handle)
    {
        aacEncClose(&handle);
    }
    handle = nullptr;
    return 0;
}

int AACEncoder::encode(IMPAudioFrame *data, unsigned char *outbuf, int *outLen)
{
    AACENC_BufDesc inBufDesc = {0}, outBufDesc = {0};
    AACENC_InArgs inArgs = {0};
    AACENC_OutArgs outArgs = {0};
    int inBufferIdentifiers[] = {IN_AUDIO_DATA};
    int inBufferElementSize[] = {sizeof(int16_t)};
    void *inBuffer[] = {data->virAddr};
    int outBufferIdentifiers[] = {OUT_BITSTREAM_DATA};
    int outBufferElementSize[] = {1};
    void *outBuffer[] = {outbuf};

    inBufDesc.numBufs = 1;
    inBufDesc.bufs = inBuffer;
    inBufDesc.bufferIdentifiers = inBufferIdentifiers;
    inBufDesc.bufSizes = &data->len;
    inBufDesc.bufElSizes = inBufferElementSize;

    outBufDesc.numBufs = 1;
    outBufDesc.bufs = outBuffer;
    outBufDesc.bufferIdentifiers = outBufferIdentifiers;
    outBufDesc.bufSizes = outLen;
    outBufDesc.bufElSizes = outBufferElementSize;

    inArgs.numInSamples = data->len / sizeof(int16_t);

    if (aacEncEncode(handle, &inBufDesc, &outBufDesc, &inArgs, &outArgs) != AACENC_OK)
    {
        LOG_WARN("Encoding failed.");
        return -1;
    }

    *outLen = outArgs.numOutBytes;
    return 0;
}
