#ifndef BACKCHANNEL_STREAM_STATE_HPP
#define BACKCHANNEL_STREAM_STATE_HPP

#include "Logger.hpp"

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <NetAddress.hh> // For sockaddr_storage, Port
#include <Boolean.hh>    // For Boolean type

// Forward declarations
class BackchannelServerMediaSubsession;
class BackchannelSink;
class RTPSource;
class Groupsock;
class RTCPInstance;
class TLSState;
class TaskScheduler; // For TaskFunc
// Removed incorrect forward declaration of ServerRequestAlternativeByteHandler (it's a typedef)
// Ensure RTPInterface.hh (where it's defined) is included where needed, likely via liveMedia.hh

// Removed BackchannelDestinations struct definition - managed internally now

class BackchannelStreamState {
    friend class BackchannelServerMediaSubsession;

public:
    // Constructor now takes destination details
    BackchannelStreamState(BackchannelServerMediaSubsession& _master,
                           RTPSource* _rtpSource, BackchannelSink* _mediaSink,
                           Groupsock* _rtpGS, Groupsock* _rtcpGS, unsigned _clientSessionId,
                           // Destination parameters:
                           Boolean _isTCP,
                           struct sockaddr_storage const& _destAddr, // UDP dest addr
                           Port const& _rtpDestPort,                // UDP RTP port
                           Port const& _rtcpDestPort,               // UDP RTCP port
                           int _tcpSocketNum,                       // TCP socket
                           unsigned char _rtpChannelId,             // TCP RTP channel
                           unsigned char _rtcpChannelId,            // TCP RTCP channel
                           TLSState* _tlsState);                    // TCP TLS state

    ~BackchannelStreamState();

    // startPlaying no longer needs BackchannelDestinations*
    void startPlaying(TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
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

    // Stored destination details:
    Boolean fIsTCP;
    struct sockaddr_storage fDestAddr; // UDP
    Port fRtpDestPort;                 // UDP
    Port fRtcpDestPort;                // UDP
    int fTcpSocketNum;                 // TCP
    unsigned char fRtpChannelId;       // TCP
    unsigned char fRtcpChannelId;      // TCP
    TLSState* fTlsState;               // TCP
};

#endif // BACKCHANNEL_STREAM_STATE_HPP
