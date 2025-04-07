#ifndef BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP
#define BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP

#define MAX_CNAME_LEN 100

#include "OnDemandServerMediaSubsession.hh" // Changed from ServerMediaSession.hh
#include "BasicUsageEnvironment.hh"
#include "RTPInterface.hh"
#include "RTPSource.hh" // Keep for createNewRTPSource override

#include "BackchannelSink.hpp"
#include "IMPBackchannel.hpp"

struct sockaddr_storage;
class Port;
class TLSState;

class RTPSink; // Keep for createNewRTPSink override
class RTPSource; // Keep for createNewRTPSource override
class Groupsock;
class FramedSource; // Keep for createNewStreamSource override
class MediaSink; // Keep for createNewStreamDestination (will rename)

#include "BackchannelStreamState.hpp"

  class BackchannelServerMediaSubsession : public OnDemandServerMediaSubsession { // Changed base class
  friend class BackchannelStreamState;
  public:
      static BackchannelServerMediaSubsession* createNew(UsageEnvironment& env, IMPBackchannelFormat format);

      virtual ~BackchannelServerMediaSubsession();

  // Keep closeStreamSource as it's called by BackchannelStreamState destructor
  virtual void closeStreamSource(FramedSource *inputSource);
  // closeStreamSink is removed - cleanup handled by deleteStream -> ~BackchannelStreamState

  protected:
      // Constructor needs to match OnDemandServerMediaSubsession requirements
      BackchannelServerMediaSubsession(UsageEnvironment& env, IMPBackchannelFormat format);

     // --- Re-defined virtual functions from OnDemandServerMediaSubsession ---
     virtual char const* sdpLines(int addressFamily); // Already overridden
     virtual void getStreamParameters(unsigned clientSessionId,
                                     struct sockaddr_storage const& clientAddress, // Already overridden
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
                                     void*& streamToken); // Already overridden
     virtual void startStream(unsigned clientSessionId, void* streamToken,
                             TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                             unsigned short& rtpSeqNum, unsigned& rtpTimestamp,
                             ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                             void* serverRequestAlternativeByteHandlerClientData); // Already overridden
     virtual void getRTPSinkandRTCP(void* streamToken, RTPSink*& rtpSink, RTCPInstance*& rtcp); // Already overridden
     virtual void deleteStream(unsigned clientSessionId, void*& streamToken); // NEW: Must implement for cleanup

     // --- New virtual functions required by OnDemandServerMediaSubsession ---
     virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate); // NEW: Must implement
     virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource); // NEW: Must implement

     // --- Existing helper/overridden methods specific to our implementation ---
     virtual MediaSink* createNewStreamDestination(unsigned clientSessionId, unsigned& estBitrate); // Keep for now, called by getStreamParameters
     virtual RTPSource* createNewRTPSource(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, MediaSink* outputSink); // Already overridden
     virtual char const* getAuxSDPLine(RTPSink* rtpSink, FramedSource* inputSource); // Already overridden

private:
    char* fSDPLines = nullptr;
    // fDestinationsHashTable is managed by OnDemandServerMediaSubsession base class now
    // HashTable* fDestinationsHashTable; // REMOVED
    char fCNAME[MAX_CNAME_LEN+1]; // Keep for RTCP
     // Re-add members needed for our custom port allocation logic
     portNumBits fInitialPortNum;
     Boolean fMultiplexRTCPWithRTP;
     IMPBackchannelFormat fFormat; // Keep specific format info
 };

#endif // BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP
