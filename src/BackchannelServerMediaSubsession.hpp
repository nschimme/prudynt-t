#ifndef BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP
#define BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP

#define MAX_CNAME_LEN 100

#include "ServerMediaSession.hh"
#include "BasicUsageEnvironment.hh"
#include "RTPInterface.hh"
#include "RTPSource.hh"

#include "BackchannelSink.hpp"

struct sockaddr_storage;
class Port;
class TLSState;

class RTPSink;
class RTPSource;
class Groupsock;
class FramedSource;
class MediaSink;

#include "BackchannelStreamState.hpp"

 class BackchannelServerMediaSubsession : public ServerMediaSubsession {
 friend class BackchannelStreamState;
 public:
     static BackchannelServerMediaSubsession* createNew(UsageEnvironment& env, Boolean reuseFirstSource = False);

     virtual ~BackchannelServerMediaSubsession();

public:
    virtual void closeStreamSource(FramedSource *inputSource);
    virtual void closeStreamSink(MediaSink *outputSink);

 protected:
     BackchannelServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource);

    virtual char const* sdpLines(int addressFamily);
    virtual void getStreamParameters(unsigned clientSessionId,
                                     struct sockaddr_storage const& clientAddress,
                                     Port const& clientRTPPort,
                                     Port const& clientRTCPPort,
                                     int tcpSocketNum,
                                     unsigned char rtpChannelId,
                                     unsigned char rtcpChannelId,
                                     TLSState* tlsState,
                                     struct sockaddr_storage& destinationAddress,
                                     u_int8_t& destinationTTL,
                                     Boolean& isMulticast,
                                     Port& serverRTPPort,
                                     Port& serverRTCPPort,
                                     void*& streamToken);
    virtual void startStream(unsigned clientSessionId, void* streamToken,
                             TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                             unsigned short& rtpSeqNum, unsigned& rtpTimestamp,
                             ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                             void* serverRequestAlternativeByteHandlerClientData);
    virtual void getRTPSinkandRTCP(void* streamToken, RTPSink*& rtpSink, RTCPInstance*& rtcp);

    virtual Boolean getServerPorts(Port& rtpPort, Port& rtcpPort);

    virtual MediaSink* createNewStreamDestination(unsigned clientSessionId, unsigned& estBitrate);
    virtual RTPSource* createNewRTPSource(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, MediaSink* outputSink);
    virtual char const* getAuxSDPLine(RTPSink* rtpSink, FramedSource* inputSource);

    virtual void deleteStreamState(void*& streamToken);


private:
    char* fSDPLines = nullptr;
     HashTable* fDestinationsHashTable;
     char fCNAME[MAX_CNAME_LEN+1];
     void* fLastStreamToken;
     Boolean fReuseFirstSource;
     portNumBits fInitialPortNum;
     Boolean fMultiplexRTCPWithRTP;
};

#endif // BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP
