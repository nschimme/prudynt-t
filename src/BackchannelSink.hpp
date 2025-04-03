#ifndef BACKCHANNEL_SINK_HPP
#define BACKCHANNEL_SINK_HPP

#include "MediaSink.hh"
#include "Boolean.hh"
#include "Logger.hpp" // Assuming Logger.hpp exists for logging

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

class BackchannelSink : public MediaSink {
public:
    static BackchannelSink* createNew(UsageEnvironment& env,
                                      const char* targetIp,
                                      portNumBits targetPort,
                                      unsigned char rtpPayloadFormat);

protected:
    BackchannelSink(UsageEnvironment& env, const char* targetIp, portNumBits targetPort, unsigned char rtpPayloadFormat);
    // called only by createNew();
    virtual ~BackchannelSink();

    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned durationInMicroseconds);
    virtual void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
                                   struct timeval presentationTime, unsigned durationInMicroseconds);

    // Implementation of virtual functions:
    virtual bool continuePlaying();
    virtual void stopPlaying();

private:
    bool connectToTarget();
    void closeConnection();
    bool sendData(u_int8_t* data, unsigned size);

    u_int8_t* fReceiveBuffer;
    static const unsigned kReceiveBufferSize = 1024; // Adjust as needed for expected packet size
    unsigned char fRtpPayloadFormat;
    char* fTargetIp;
    portNumBits fTargetPort;
    int fSocket;
    bool fIsConnected;
    struct sockaddr_in fTargetAddr;
};

#endif // BACKCHANNEL_SINK_HPP
