#ifndef BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP
#define BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP

#define MAX_CNAME_LEN 100 // Define max CNAME length

// Live555 Headers
#include "ServerMediaSession.hh" // Changed base class include
#include "BasicUsageEnvironment.hh"
#include "RTPInterface.hh" // For RTPSink definition (though not used directly)
#include "RTPSource.hh" // Needed for RTPSource type definition

// Project Headers
// #include "BackchannelSourceSelector.hpp" // Removed
#include "BackchannelSink.hpp" // Include the renamed sink

// Forward declarations for types used in getStreamParameters
struct sockaddr_storage;
class Port;
class TLSState;

// Forward declarations from Live555 needed for method signatures
class RTPSink;
class RTPSource;
class Groupsock;
class FramedSource;
class MediaSink; // BackchannelSink will inherit from this

// Include the new state header
#include "BackchannelStreamState.hpp"

 class BackchannelServerMediaSubsession : public ServerMediaSubsession { // Changed base class
 friend class BackchannelStreamState; // Grant access to private members like fCNAME (Changed struct to class)
 public:
     // Constructor now takes reuseFirstSource flag
     static BackchannelServerMediaSubsession* createNew(UsageEnvironment& env, backchannel_stream* stream_data, Boolean reuseFirstSource = False); // Default to FALSE

     virtual ~BackchannelServerMediaSubsession();

// Make helper methods public so StreamState can call them
public:
    // Declare closeStreamSource/Sink as required by base class (even if trivial)
    virtual void closeStreamSource(FramedSource *inputSource);
    virtual void closeStreamSink(MediaSink *outputSink);

 protected: // Called only by createNew()
     BackchannelServerMediaSubsession(UsageEnvironment& env, backchannel_stream* stream_data, Boolean reuseFirstSource);

     // --- Required virtual methods from ServerMediaSubsession ---
    virtual char const* sdpLines(int addressFamily); // Added addressFamily parameter
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
    virtual void getRTPSinkandRTCP(void* streamToken, RTPSink*& rtpSink, RTCPInstance*& rtcp); // Added declaration
    // Note: pauseStream, seekStream, getStreamEndTime are optional

    // Helper to get ports for reuse logic
    virtual Boolean getServerPorts(Port& rtpPort, Port& rtcpPort);

    // --- Methods needed for the explicit source/sink creation pattern ---
    // Creates the MediaSink (our BackchannelSink)
    virtual MediaSink* createNewStreamDestination(unsigned clientSessionId, unsigned& estBitrate);
    // Creates the RTPSource (e.g., SimpleRTPSource)
    virtual RTPSource* createNewRTPSource(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, MediaSink* outputSink);
    // Helper for SDP generation (from example) - Note: Signature differs from example, adjust if needed
    virtual char const* getAuxSDPLine(RTPSink* rtpSink, FramedSource* inputSource);

    // --- Helper methods for stream state cleanup ---
    virtual void deleteStreamState(void*& streamToken);
    // Moved closeStreamSource/Sink declarations to public section above


private:
    char* fSDPLines = nullptr; // Buffer to hold generated SDP lines, initialize to nullptr
    backchannel_stream* fStreamData; // Store pointer to shared stream data needed by sink
    // HashTable* fStreamStates; // Removed - state managed via streamToken
     HashTable* fDestinationsHashTable; // Table to hold BackchannelDestinations objects keyed by clientSessionId
     char fCNAME[MAX_CNAME_LEN+1]; // Added CNAME member for RTCP
     void* fLastStreamToken; // Pointer to the last created stream state (mirroring example) - Init in constructor
     Boolean fReuseFirstSource; // Added flag
     // --- Members added from reference OnDemandServerMediaSubsession_BC ---
     portNumBits fInitialPortNum;
     Boolean fMultiplexRTCPWithRTP;
     // --- End added members ---
};

#endif // BACKCHANNEL_SERVER_MEDIA_SUBSESSION_HPP
