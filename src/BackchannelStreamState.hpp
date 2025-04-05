#ifndef BACKCHANNEL_STREAM_STATE_HPP
#define BACKCHANNEL_STREAM_STATE_HPP

// Project Headers
#include "Logger.hpp" // Include Logger for LOG_DEBUG macro

// Live555 Headers
#include <liveMedia.hh> // For Medium, RTCPInstance, Groupsock, RTPSource, TaskScheduler, Port
#include <BasicUsageEnvironment.hh> // For UsageEnvironment
#include <NetAddress.hh> // For struct sockaddr_storage, Port

// Forward declarations
class BackchannelServerMediaSubsession; // Needed for master reference and fCNAME access
class BackchannelSink; // Needed for mediaSink member
class RTPSource;
class Groupsock;
class RTCPInstance;
class TLSState; // Forward declare for BackchannelDestinations class

// Class to hold client destination information (Changed from struct)
class BackchannelDestinations {
public: // Members and constructors need to be public
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
    LOG_DEBUG(">>> BackchannelDestinations (UDP) created: " << (int)this); // Added log
  }

  BackchannelDestinations(int _tcpSocketNum, unsigned char _rtpChannelId, unsigned char _rtcpChannelId, TLSState* _tlsState)
    : rtpPort(0), rtcpPort(0), isTCP(True), tcpSocketNum(_tcpSocketNum),
      rtpChannelId(_rtpChannelId), rtcpChannelId(_rtcpChannelId), tlsState(_tlsState) {
    memset(&addr, 0, sizeof(addr)); // Clear address field for TCP
    LOG_DEBUG(">>> BackchannelDestinations (TCP) created: " << (int)this); // Added log
  }

  ~BackchannelDestinations() { // Added destructor log
      LOG_DEBUG(">>> BackchannelDestinations destroyed: " << (int)this);
  }
};


// Class to hold state for each client stream (Changed from struct)
class BackchannelStreamState {
    // Grant access to BackchannelServerMediaSubsession for cleanup and potentially other needs
    friend class BackchannelServerMediaSubsession;

public: // Public interface
    BackchannelStreamState(BackchannelServerMediaSubsession& _master,
                           RTPSource* _rtpSource, BackchannelSink* _mediaSink,
                           Groupsock* _rtpGS, Groupsock* _rtcpGS, unsigned _clientSessionId);

    ~BackchannelStreamState();

    // Method to start the data flow and RTCP, now takes BackchannelDestinations and byte handler params
    void startPlaying(BackchannelDestinations* dests, TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                      ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                      void* serverRequestAlternativeByteHandlerClientData);

    // Reference counting for reuse logic
    void incrementReferenceCount() { ++fReferenceCount; }
    unsigned referenceCount() const { return fReferenceCount; }
    unsigned decrementReferenceCount() { return --fReferenceCount; }


private: // Implementation details
    BackchannelServerMediaSubsession& master; // Reference to the parent subsession
    RTPSource* rtpSource;                     // The source receiving RTP from the client
    BackchannelSink* mediaSink;               // The sink processing the received data
    Groupsock* rtpGS;                         // Groupsock for RTP (NULL if TCP)
    Groupsock* rtcpGS;                        // Groupsock for RTCP (NULL if TCP)
    RTCPInstance* rtcpInstance;               // RTCP instance for this stream
    unsigned clientSessionId;                 // Client session ID for logging/tracking
    unsigned fReferenceCount;                 // Reference count for reuse logic
};

#endif // BACKCHANNEL_STREAM_STATE_HPP
