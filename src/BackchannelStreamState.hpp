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
// ServerRequestAlternativeByteHandler is a typedef defined in RTPInterface.hh (included via liveMedia.hh)

// Define structs for transport details
struct UdpTransportDetails {
    struct sockaddr_storage destAddr;
    Port rtpDestPort;
    Port rtcpDestPort;
};

struct TcpTransportDetails {
    int tcpSocketNum;
    unsigned char rtpChannelId;
    unsigned char rtcpChannelId;
    TLSState* tlsState;
};

// Define the union
union TransportSpecificDetails {
    UdpTransportDetails u; // UDP details
    TcpTransportDetails t; // TCP details

    // Provide default constructor/destructor for the union
    TransportSpecificDetails() { /* memset(this, 0, sizeof(*this)); // Optional: Zero-initialize */ }
    ~TransportSpecificDetails() {}
};


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

    // Transport details stored in a union
    Boolean fIsTCP;                     // Flag to indicate active union member
    TransportSpecificDetails fTransport; // Union holding either UDP or TCP details
};

#endif // BACKCHANNEL_STREAM_STATE_HPP
