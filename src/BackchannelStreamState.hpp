#ifndef BACKCHANNEL_STREAM_STATE_HPP
#define BACKCHANNEL_STREAM_STATE_HPP

#include "Logger.hpp"

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <NetAddress.hh>

class BackchannelServerMediaSubsession;
class BackchannelSink;
class RTPSource;
class Groupsock;
class RTCPInstance;
class TLSState;

class BackchannelDestinations {
public:
  struct sockaddr_storage addr;
  Port rtpPort;
  Port rtcpPort;
  Boolean isTCP;
  int tcpSocketNum;
  unsigned char rtpChannelId;
  unsigned char rtcpChannelId;
  TLSState* tlsState;

  BackchannelDestinations(struct sockaddr_storage const& _addr, Port const& _rtpPort, Port const& _rtcpPort)
    : rtpPort(_rtpPort), rtcpPort(_rtcpPort), isTCP(False), tcpSocketNum(-1),
      rtpChannelId(0), rtcpChannelId(0), tlsState(nullptr) {
    addr = _addr;
    LOG_DEBUG(">>> BackchannelDestinations (UDP) created: " << (int)this);
  }

  BackchannelDestinations(int _tcpSocketNum, unsigned char _rtpChannelId, unsigned char _rtcpChannelId, TLSState* _tlsState)
    : rtpPort(0), rtcpPort(0), isTCP(True), tcpSocketNum(_tcpSocketNum),
      rtpChannelId(_rtpChannelId), rtcpChannelId(_rtcpChannelId), tlsState(_tlsState) {
    memset(&addr, 0, sizeof(addr));
    LOG_DEBUG(">>> BackchannelDestinations (TCP) created: " << (int)this);
  }

  ~BackchannelDestinations() {
      LOG_DEBUG(">>> BackchannelDestinations destroyed: " << (int)this);
  }
};


class BackchannelStreamState {
    friend class BackchannelServerMediaSubsession;

public:
    BackchannelStreamState(BackchannelServerMediaSubsession& _master,
                           RTPSource* _rtpSource, BackchannelSink* _mediaSink,
                           Groupsock* _rtpGS, Groupsock* _rtcpGS, unsigned _clientSessionId);

    ~BackchannelStreamState();

    void startPlaying(BackchannelDestinations* dests, TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                      ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                      void* serverRequestAlternativeByteHandlerClientData);

private:
    BackchannelServerMediaSubsession& master;
    RTPSource* rtpSource;
    BackchannelSink* mediaSink;
    Groupsock* rtpGS;
    Groupsock* rtcpGS;
    RTCPInstance* rtcpInstance;
    unsigned clientSessionId;
};

#endif // BACKCHANNEL_STREAM_STATE_HPP
