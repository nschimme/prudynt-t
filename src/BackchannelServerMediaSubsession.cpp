#include "BackchannelServerMediaSubsession.hpp"
#include "BackchannelSink.hpp"
#include "Logger.hpp"
#include "globals.hpp" // Needed for cfg
#include "Config.hpp"  // Needed for cfg
#include "IMPBackchannel.hpp"

#include <NetAddress.hh>
#include <liveMedia.hh> // Includes ServerMediaSubsession.hh, OnDemandServerMediaSubsession.hh etc.
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <SimpleRTPSource.hh>
// #include <HashTable.hh> // Base class handles destination hash table
#include "BackchannelStreamState.hpp"

#include <cassert> // Keep for potential asserts
#include <unistd.h>
#include <cstring>
#include <sys/time.h>
#include <string> // Needed for std::string

#define MODULE "BackchannelSubsession"

 BackchannelServerMediaSubsession* BackchannelServerMediaSubsession::createNew(UsageEnvironment& env, IMPBackchannelFormat format /*, Boolean reuseFirstSource - Removed */) {
     return new BackchannelServerMediaSubsession(env, format);
 }

 // Constructor needs to match OnDemandServerMediaSubsession requirements
 BackchannelServerMediaSubsession::BackchannelServerMediaSubsession(UsageEnvironment& env, IMPBackchannelFormat format)
     : OnDemandServerMediaSubsession(env, False /*reuseFirstSource*/), // Call base constructor
       fSDPLines(nullptr), fFormat(format),
       // Initialize our local members
       fInitialPortNum(6970), // Default starting port (same as before)
       fMultiplexRTCPWithRTP(False) // Default multiplexing (same as before)
 {
     LOG_DEBUG("Subsession created for channel " << static_cast<int>(fFormat));
    // fDestinationsHashTable managed by base class
    gethostname(fCNAME, MAX_CNAME_LEN); // Get CNAME for RTCP
    fCNAME[MAX_CNAME_LEN] = '\0';

    // Adjust initial port if not multiplexing (same logic as before)
    if (!fMultiplexRTCPWithRTP) {
        fInitialPortNum = (fInitialPortNum + 1) & ~1;
    }
}

BackchannelServerMediaSubsession::~BackchannelServerMediaSubsession() {
    LOG_DEBUG("Subsession destroyed");
    delete[] fSDPLines;
    // fDestinationsHashTable and its contents managed by base class
}

// --- Re-defined virtual functions from OnDemandServerMediaSubsession ---

char const* BackchannelServerMediaSubsession::sdpLines(int /*addressFamily*/) {
    // This implementation remains the same
    if (fSDPLines == nullptr) {
        LOG_DEBUG("Generating SDP lines");

        // Increase size for potential fmtp line
        unsigned int sdpLinesSize = 400; 
        fSDPLines = new char[sdpLinesSize];
        if (fSDPLines == nullptr) return nullptr;

        const char* formatName;
        int payloadType;
        unsigned frequency;

        #define APPLY_SDP_IF(EnumName, NameString, PayloadType, Frequency, MimeType) \
            if (fFormat == IMPBackchannelFormat::EnumName) { \
                formatName = NameString; \
                payloadType = PayloadType; \
                frequency = Frequency; \
            }

        X_FOREACH_BACKCHANNEL_FORMAT(APPLY_SDP_IF)
        #undef APPLY_SDP_IF

        LOG_DEBUG("Generating SDP for format " << formatName << " (Payload Type: " << payloadType << ")");

        // Construct fmtp line only for Opus
        std::string fmtpLine = "";
        if (fFormat == IMPBackchannelFormat::OPUS) {
            char fmtpBuf[100];
            snprintf(fmtpBuf, sizeof(fmtpBuf),
                     "a=fmtp:%d stereo=1; maxplaybackrate=%d; sprop-maxcapturerate=%d\r\n",
                     payloadType, cfg->audio.output_sample_rate, cfg->audio.output_sample_rate);
            fmtpLine = fmtpBuf;
        }

        snprintf(fSDPLines, sdpLinesSize,
                 "m=audio 0 RTP/AVP %d\r\n"
                 "c=IN IP4 0.0.0.0\r\n"
                 "b=AS:64\r\n" // Assuming bitrate is OK for now
                 "a=rtpmap:%d %s/%u/%d\r\n"
                 "%s" // Placeholder for fmtp line
                 "a=control:%s\r\n"
                 "a=sendonly\r\n",
                 payloadType,
                 payloadType, formatName, frequency,
                 (fFormat == IMPBackchannelFormat::OPUS) ? 2 : 1,
                 fmtpLine.c_str(),
                 trackId()
         );

        fSDPLines[sdpLinesSize - 1] = '\0'; // Ensure null termination
        LOG_DEBUG("Backchannel SDP: " << fSDPLines);
    }
     return fSDPLines;
 }

 // Keep this helper method, though it's not a direct override anymore
 MediaSink* BackchannelServerMediaSubsession::createNewStreamDestination(unsigned clientSessionId, unsigned& estBitrate) {
     // Estimate bitrate based on format? Opus can be lower.
     if (fFormat == IMPBackchannelFormat::OPUS) {
         estBitrate = 32; // Example Opus bitrate
    } else {
         estBitrate = 64; // G.711 bitrate
    }
    LOG_INFO("Creating BackchannelSink for channel: " << static_cast<int>(fFormat) << " (est bitrate: " << estBitrate << ")");
     return BackchannelSink::createNew(envir(), clientSessionId, fFormat);
 }

 // Keep this helper method, though it's not a direct override anymore
 RTPSource* BackchannelServerMediaSubsession::createNewRTPSource(Groupsock* rtpGroupsock, unsigned char /*rtpPayloadTypeIfDynamic*/, MediaSink* /*outputSink*/) {
     LOG_DEBUG("Creating new RTPSource for channel " << static_cast<int>(fFormat));

     const char* mimeType;
    int payloadType;
    unsigned frequency;
    unsigned numChannels = 0;

    #define APPLY_SOURCE_IF(EnumName, NameString, PayloadType, Frequency, MimeType) \
        if (fFormat == IMPBackchannelFormat::EnumName) { \
            mimeType = MimeType; \
            payloadType = PayloadType; \
            frequency = Frequency; \
            /* Although SDP rtpmap says /1, the actual source might need /2 if decoder outputs stereo? */ \
            /* Let's keep this 1 for now as we downmix later */ \
            /* if (fFormat == IMPBackchannelFormat::OPUS) numChannels = 2; */ \
        }

    X_FOREACH_BACKCHANNEL_FORMAT(APPLY_SOURCE_IF)
    #undef APPLY_SOURCE_IF

    LOG_INFO("Creating SimpleRTPSource with payloadType=" << payloadType << ", frequency=" << frequency << ", mimeType=" << mimeType << ", numChannels=" << numChannels);
    return SimpleRTPSource::createNew(envir(), rtpGroupsock,
                                      payloadType,
                                      frequency,
                                      mimeType,
                                      0, // Use determined channel count
                                       False); // allowMultipleFramesPerPacket
 }

 char const* BackchannelServerMediaSubsession::getAuxSDPLine(RTPSink* /*rtpSink*/, FramedSource* /*inputSource*/) {
     // This implementation remains the same
     // Could potentially add fmtp line here instead of sdpLines if needed
     return nullptr;
 }

 void BackchannelServerMediaSubsession::getStreamParameters(unsigned clientSessionId,
                                     struct sockaddr_storage const& clientAddress,
                                     Port const& clientRTPPort,
                                     Port const& clientRTCPPort,
                                     int tcpSocketNum,
                                     unsigned char rtpChannelId,
                                     unsigned char rtcpChannelId,
                                     TLSState* tlsState,
                                     struct sockaddr_storage& destinationAddress,
                                     u_int8_t& /*destinationTTL*/,
                                     Boolean& isMulticast,
                                     Port& serverRTPPort,
                                     Port& serverRTCPPort,
                                     void*& streamToken)
 {
     // This method sets up the RTP/RTCP ports and creates our BackchannelStreamState
     // It's largely the same, but we don't manage the destinations hash table anymore.
     LOG_DEBUG("getStreamParameters input for session " << clientSessionId
              << ": clientRTPPort=" << ntohs(clientRTPPort.num())
              << ", clientRTCPPort=" << ntohs(clientRTCPPort.num())
              << ", tcpSocketNum=" << tcpSocketNum);

     LOG_DEBUG("getStreamParameters for session " << clientSessionId);
     isMulticast = False; // We are always unicast
     streamToken = nullptr; // We will assign our BackchannelStreamState here

     // Set destination address if not provided (standard practice)
     if (addressIsNull(destinationAddress)) {
         destinationAddress = clientAddress;
     }

     // Create our specific sink and source
     LOG_DEBUG("Creating sink/source state for session " << clientSessionId);
     unsigned streamBitrate = 0; // Will be set by createNewStreamDestination
     MediaSink* mediaSink = nullptr;
     RTPSource* rtpSource = nullptr;
     Groupsock* rtpGroupsock = nullptr;
     Groupsock* rtcpGroupsock = nullptr;

     LOG_DEBUG("Calling createNewStreamDestination (our helper) for session " << clientSessionId);
     mediaSink = createNewStreamDestination(clientSessionId, streamBitrate); // Use our helper
     if (mediaSink == nullptr) {
         LOG_ERROR(">>> getStreamParameters: createNewStreamDestination (helper) FAILED for session " << clientSessionId);
         return; // Cannot proceed without sink
     }
     LOG_DEBUG("createNewStreamDestination (helper) succeeded for session " << clientSessionId << ". Sink: " << (int)mediaSink);

     // Set up transport (UDP or TCP) - mostly same logic
     if (clientRTPPort.num() != 0 || tcpSocketNum >= 0) {
         // Client has provided transport parameters
         if (clientRTCPPort.num() == 0 && tcpSocketNum < 0) {
              // Raw UDP (RTP only) - Not supported for backchannel needing RTCP?
              LOG_ERROR("Raw UDP streaming (no RTCP port) might not be fully supported for backchannel");
              // Proceeding anyway, but RTCP might fail later
              // if (mediaSink) Medium::close(mediaSink); // Don't close yet
              // return;
         }

         if (tcpSocketNum < 0) { // UDP
             LOG_DEBUG("Attempting UDP port allocation for session " << clientSessionId);
             NoReuse dummy(envir());
              // Get initial port from our local member
              portNumBits serverPortNum = fInitialPortNum;
              // Check multiplexing setting from our local member
              Boolean multiplexRTCP = fMultiplexRTCPWithRTP;

              while(1) {
                 serverRTPPort = serverPortNum;
                 struct sockaddr_storage nullAddr; // Bind to 0.0.0.0
                 memset(&nullAddr, 0, sizeof(nullAddr));
                 nullAddr.ss_family = AF_INET; // Or AF_INET6 if needed
                 rtpGroupsock = createGroupsock(nullAddr, serverRTPPort); // Use base class virtual method
                 if (rtpGroupsock->socketNum() < 0) {
                     delete rtpGroupsock; rtpGroupsock = nullptr;
                     serverPortNum += (multiplexRTCP ? 1 : 2);
                     continue; // Try next port
                 }

                 if (multiplexRTCP) {
                     serverRTCPPort = serverRTPPort;
                     rtcpGroupsock = rtpGroupsock;
                 } else {
                     serverRTCPPort = ++serverPortNum;
                     rtcpGroupsock = createGroupsock(nullAddr, serverRTCPPort); // Use base class virtual method
                     if (rtcpGroupsock->socketNum() < 0) {
                         // Failed to get RTCP port, clean up RTP port too
                         delete rtpGroupsock; rtpGroupsock = nullptr;
                         delete rtcpGroupsock; rtcpGroupsock = nullptr;
                         ++serverPortNum; // Skip this pair
                         continue; // Try next port pair
                     }
                  }
                  break; // Successfully allocated ports
              }

              // Increase buffer size (optional but good)
              if (rtpGroupsock != nullptr && rtpGroupsock->socketNum() >= 0) {
                  unsigned requestedSize = 262144; // Same as before
                  LOG_INFO("Attempting to increase RTP socket receive buffer for session " << clientSessionId << " to " << requestedSize);
                  increaseReceiveBufferTo(envir(), rtpGroupsock->socketNum(), requestedSize);
              }
              LOG_DEBUG("UDP port allocation succeeded for session " << clientSessionId << ". RTP=" << ntohs(serverRTPPort.num()) << ", RTCP=" << ntohs(serverRTCPPort.num()));

          } else { // TCP
               LOG_DEBUG("Client requested TCP interleaved mode for session " << clientSessionId);
              // For TCP, the base class likely handles socket setup. We just need dummy groupsocks.
              serverRTPPort = 0; // Indicate TCP
              serverRTCPPort = 0; // Indicate TCP
              struct sockaddr_storage dummyAddr; memset(&dummyAddr, 0, sizeof dummyAddr); dummyAddr.ss_family = AF_INET;
              Port dummyPort(0);
              // Create dummy groupsocks - needed for RTPSource/RTCPInstance potentially
              rtpGroupsock = new Groupsock(envir(), dummyAddr, dummyPort, 0);
              rtcpGroupsock = new Groupsock(envir(), dummyAddr, dummyPort, 0);
              LOG_DEBUG("Created dummy RTP Groupsock (" << (int)rtpGroupsock << ") and dummy RTCP Groupsock (" << (int)rtcpGroupsock << ") for TCP mode.");
         }

         // Create the RTPSource (which receives data)
         LOG_DEBUG("Calling createNewRTPSource (our helper) for session " << clientSessionId);
         rtpSource = createNewRTPSource(rtpGroupsock, 0, mediaSink); // Use our helper
         if (rtpSource == nullptr) {
             LOG_ERROR(">>> getStreamParameters: createNewRTPSource (helper) FAILED for session " << clientSessionId);
             if (mediaSink) Medium::close(mediaSink);
             delete rtpGroupsock;
             if (rtcpGroupsock != rtpGroupsock) delete rtcpGroupsock;
             return; // Cannot proceed without source
         }
          LOG_DEBUG("createNewRTPSource (helper) succeeded for session " << clientSessionId << ". Source: " << (int)rtpSource);

     } else {
          // Invalid parameters from client
          LOG_ERROR(">>> getStreamParameters: Invalid parameters (no client ports or TCP socket) for session " << clientSessionId);
          if (mediaSink) Medium::close(mediaSink); // Clean up sink if created
          return;
      }

      // Create our custom stream state object, passing destination details
      LOG_DEBUG("Creating BackchannelStreamState for session " << clientSessionId);
      Boolean isTCP = (tcpSocketNum >= 0);
      BackchannelStreamState* state = new BackchannelStreamState(*this, rtpSource, (BackchannelSink*)mediaSink, rtpGroupsock, rtcpGroupsock, clientSessionId,
                                                                 // Destination parameters:
                                                                 isTCP,
                                                                 destinationAddress, // UDP dest addr (used even if TCP for RTCP handler?)
                                                                 clientRTPPort,      // UDP RTP port
                                                                 clientRTCPPort,     // UDP RTCP port
                                                                 tcpSocketNum,       // TCP socket
                                                                 rtpChannelId,       // TCP RTP channel
                                                                 rtcpChannelId,      // TCP RTCP channel
                                                                 tlsState);          // TCP TLS state
      streamToken = (void*)state; // Assign our state object to the stream token
      LOG_DEBUG("Created BackchannelStreamState for session " << clientSessionId << ". State: " << (int)state << ". Assigned to streamToken.");

      // Destination management is now handled by the base class using its internal hash table.
     // We don't create or add BackchannelDestinations objects here anymore.

    LOG_DEBUG("getStreamParameters complete for session " << clientSessionId << ". streamToken: " << (int)streamToken);
 }


 void BackchannelServerMediaSubsession::startStream(unsigned clientSessionId, void* streamToken,
                              TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                              unsigned short& rtpSeqNum, unsigned& rtpTimestamp,
                              ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                              void* serverRequestAlternativeByteHandlerClientData)
 {
    // This method starts the actual data flow.
    LOG_DEBUG("startStream for session " << clientSessionId << ". streamToken: " << (int)streamToken);
    BackchannelStreamState* state = (BackchannelStreamState*)streamToken; // Cast to our state type

     // The BackchannelStreamState now holds the destination info internally.
     // We no longer need the Destinations* object here.

     if (state == nullptr) {
         LOG_ERROR("startStream called with NULL streamToken for client session " << clientSessionId);
         return;
     }

     // Call our state object's startPlaying method (updated signature)
     state->startPlaying(rtcpRRHandler, rtcpRRHandlerClientData,
                         serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);

     // Set initial RTP seq num and timestamp (standard practice)
     RTPSource* rtpSource = state->rtpSource; // Get source from our state
     // Removed unused sink variable
     if (rtpSource != nullptr) {
         // Initialize sequence number from the source if possible
         rtpSeqNum = rtpSource->curPacketMarkerBit() ? (rtpSource->curPacketRTPSeqNum() + 1) : rtpSource->curPacketRTPSeqNum();
         // Timestamp is private in RTPSource. Use 0 or maybe last from sink? Using 0 for now.
         rtpTimestamp = 0; // sink ? sink->lastTimestamp() : 0; // Assuming BackchannelSink has such a method
         LOG_DEBUG("startStream: Initializing rtpSeqNum=" << rtpSeqNum << ", rtpTimestamp=" << rtpTimestamp << " for session " << clientSessionId);
     } else {
         rtpSeqNum = 0; // Default if source is null (shouldn't happen)
         rtpTimestamp = 0;
         LOG_WARN("RTPSource is NULL in state for session " << clientSessionId << ". Setting SeqNum/Timestamp to 0.");
    }
 }

 void BackchannelServerMediaSubsession::deleteStream(unsigned clientSessionId, void*& streamToken) {
     // This is called by the base class when the stream is torn down.
     // We clean up our BackchannelStreamState object here.
     LOG_INFO("Deleting stream for session " << clientSessionId << ". streamToken: " << (int)streamToken);

     BackchannelStreamState* state = (BackchannelStreamState*)streamToken;
     if (state != nullptr) {
         // Deleting the state object will trigger its destructor, which handles
         // closing the sink/source and deleting groupsocks.
         delete state;
         streamToken = nullptr; // Important to nullify the token after deletion
         LOG_INFO("Deleted BackchannelStreamState for session " << clientSessionId);
     } else {
         LOG_WARN("deleteStream called with NULL streamToken for session " << clientSessionId);
     }

     // We don't call the base class deleteStream because it assumes streamToken
     // points to its internal StreamState type. Our deletion handles everything.
     // OnDemandServerMediaSubsession::deleteStream(clientSessionId, streamToken); // DO NOT CALL
 }


 void BackchannelServerMediaSubsession::getRTPSinkandRTCP(void* streamToken, RTPSink*& rtpSink, RTCPInstance*& rtcp) {
     // This subsession *receives* RTP via an RTPSource and processes it with a MediaSink (BackchannelSink).
     // It does not *send* RTP using an RTPSink.
     // It might have an RTCP instance associated with the RTPSource, though.
     rtpSink = nullptr; // We don't have an outgoing RTPSink

     BackchannelStreamState* state = (BackchannelStreamState*)streamToken;
      if (state != nullptr) {
          // Get the RTCP instance stored within our BackchannelStreamState
          rtcp = state->rtcpInstance;
          LOG_DEBUG("getRTPSinkandRTCP: Found RTCP instance " << (int)rtcp << " via BackchannelStreamState for session " << state->clientSessionId);
      } else {
         rtcp = nullptr;
         LOG_DEBUG("getRTPSinkandRTCP: No stream state or RTPSource found, returning NULL RTCP instance.");
     }
 }

 // Keep closeStreamSource as it's called by BackchannelStreamState destructor
 void BackchannelServerMediaSubsession::closeStreamSource(FramedSource *inputSource) {
      LOG_DEBUG("Closing stream source (FramedSource): " << (int)inputSource);
      Medium::close(inputSource); // Standard way to close live555 sources/sinks
 }

 // closeStreamSink removed - cleanup handled by deleteStream -> ~BackchannelStreamState


 // --- New virtual functions required by OnDemandServerMediaSubsession ---

 FramedSource* BackchannelServerMediaSubsession::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
     // This function is expected to create the source that *feeds* the RTPSink.
     // Since we are receiving, we don't have such a source.
     // However, we can use this hook to estimate the bitrate based on format.
     LOG_DEBUG("createNewStreamSource called for session " << clientSessionId);
     if (fFormat == IMPBackchannelFormat::OPUS) {
          estBitrate = 32; // Example Opus bitrate
     } else {
          estBitrate = 64; // G.711 bitrate
     }
     LOG_DEBUG("Estimated bitrate for session " << clientSessionId << ": " << estBitrate);
     return nullptr; // Return null as we don't source media this way
 }

 RTPSink* BackchannelServerMediaSubsession::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource) {
     // This function is expected to create the RTPSink for *sending* RTP.
     // Since we are receiving, we don't create an RTPSink here.
     LOG_DEBUG("createNewRTPSink called (rtpGroupsock: " << (int)rtpGroupsock << ", payloadType: " << (int)rtpPayloadTypeIfDynamic << ")");
     return nullptr; // Return null as we don't send RTP this way
 }
