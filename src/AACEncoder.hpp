#ifndef AAC_ENCODER_HPP
#define AAC_ENCODER_HPP

#include "IMPAudio.hpp"
#include <fdk-aac/aacenc_lib.h>

class AACEncoder : public IMPAudioEncoder
{
public:
    static AACEncoder* createNew(int sampleRate, int numChn);

    AACEncoder(int sampleRate, int numChn);
    virtual ~AACEncoder();

    int open() override;
    int encode(IMPAudioFrame *data, unsigned char *outbuf, int *outLen) override;
    int close() override;

private:
    HANDLE_AACENCODER handle;
    int sampleRate;
    int numChn;
};

#endif // AAC_ENCODER_HPP
