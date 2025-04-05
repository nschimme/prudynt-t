#ifndef _RTP_PACKET_VALIDATOR_FILTER_HH
#define _RTP_PACKET_VALIDATOR_FILTER_HH

#include <FramedFilter.hh>
#include "Logger.hpp" // For logging

#define RTP_VALIDATOR_FILTER_BUFFER_SIZE 200000 // Adjust buffer size if needed

class RTPPacketValidatorFilter : public FramedFilter {
public:
    static RTPPacketValidatorFilter* createNew(UsageEnvironment& env, FramedSource* inputSource);

protected:
    RTPPacketValidatorFilter(UsageEnvironment& env, FramedSource* inputSource);
    // called only by createNew()

    virtual ~RTPPacketValidatorFilter();

public: // Make doGetNextFrame public in this derived class
    // Redefined virtual functions:
    virtual void doGetNextFrame();

protected: // Keep doStopGettingFrames protected (or private)
    virtual void doStopGettingFrames();

private:
    // Callback function:
    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned durationInMicroseconds);
    void afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                            struct timeval presentationTime,
                            unsigned durationInMicroseconds);

private:
    unsigned char* fBuffer;
    unsigned fExpectedPayloadType; // Store the expected PT (e.g., 8 for PCMA)
};

#endif // _RTP_PACKET_VALIDATOR_FILTER_HH
