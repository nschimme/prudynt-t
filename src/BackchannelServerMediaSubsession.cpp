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
 #include "BackchannelStreamState.hpp"

 #include <cassert>
 #include <unistd.h>
 #include <cstring>
#include <sys/time.h>
#include <string> // Needed for std::string

#define MODULE "BackchannelSubsession"

  BackchannelServerMediaSubsession* BackchannelServerMediaSubsession::createNew(UsageEnvironment& env, IMPBackchannelFormat format) {
      return new BackchannelServerMediaSubsession(env, format);
  }

  BackchannelServerMediaSubsession::BackchannelServerMediaSubsession(UsageEnvironment& env, IMPBackchannelFormat format)
      : OnDemandServerMediaSubsession(env, False), // Base class first
        fSDPLines(nullptr),                       // Matches declaration order
        fInitialPortNum(6970),                    // Matches declaration order
        fMultiplexRTCPWithRTP(False),             // Matches declaration order
        fFormat(format)                           // Matches declaration order
  {
      LOG_DEBUG("Subsession created for channel " << static_cast<int>(fFormat));
     gethostname(fCNAME, MAX_CNAME_LEN);
     fCNAME[MAX_CNAME_LEN] = '\0';

     // Adjust initial port number for RTCP if not multiplexing
     if (!fMultiplexRTCPWithRTP) {
         fInitialPortNum = (fInitialPortNum + 1) & ~1;
     }
 }

 BackchannelServerMediaSubsession::~BackchannelServerMediaSubsession() {
     LOG_DEBUG("Subsession destroyed");
     delete[] fSDPLines;
 }

 // --- Re-defined virtual functions from OnDemandServerMediaSubsession ---

 char const* BackchannelServerMediaSubsession::sdpLines(int /*addressFamily*/) {
     if (fSDPLines == nullptr) {
         // LOG_DEBUG("Generating SDP lines"); // Removed - less verbose

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

          LOG_DEBUG("Generating SDP for format " << formatName << " (Payload Type: " << payloadType << ")"); // Removed - less verbose

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
      }
      return fSDPLines;
  }

  MediaSink* BackchannelServerMediaSubsession::createNewStreamDestination(unsigned clientSessionId, unsigned& estBitrate) {
      // Estimate bitrate based on format
      if (fFormat == IMPBackchannelFormat::OPUS) {
          estBitrate = 32;
     } else {
          estBitrate = 64;
     }
     LOG_INFO("Creating BackchannelSink for channel: " << static_cast<int>(fFormat) << " (est bitrate: " << estBitrate << ")");
      return BackchannelSink::createNew(envir(), clientSessionId, fFormat);
  }

  RTPSource* BackchannelServerMediaSubsession::createNewRTPSource(Groupsock* rtpGroupsock, unsigned char /*rtpPayloadTypeIfDynamic*/, MediaSink* /*outputSink*/) {
      // LOG_DEBUG("Creating new RTPSource for channel " << static_cast<int>(fFormat)); // Removed - less verbose

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
                                             0, // numChannels - currently always 0
                                              False); // allowMultipleFramesPerPacket
  }

  char const* BackchannelServerMediaSubsession::getAuxSDPLine(RTPSink* /*rtpSink*/, FramedSource* /*inputSource*/) {
      // No auxiliary SDP line needed
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
      // LOG_DEBUG("getStreamParameters input for session " << clientSessionId // Removed - too verbose
      //          << ": clientRTPPort=" << ntohs(clientRTPPort.num())
      //          << ", clientRTCPPort=" << ntohs(clientRTCPPort.num())
      //          << ", tcpSocketNum=" << tcpSocketNum);

      // LOG_DEBUG("getStreamParameters for session " << clientSessionId); // Removed - less verbose
      isMulticast = False; // This subsession is always unicast
      streamToken = nullptr;

      // Set destination address if not provided by client
      if (addressIsNull(destinationAddress)) {
          destinationAddress = clientAddress;
      }

      // LOG_DEBUG("Creating sink/source state for session " << clientSessionId); // Removed - less verbose
      unsigned streamBitrate = 0;
      MediaSink* mediaSink = nullptr;
      RTPSource* rtpSource = nullptr;
      Groupsock* rtpGroupsock = nullptr;
      Groupsock* rtcpGroupsock = nullptr;

      // LOG_DEBUG("Calling createNewStreamDestination (our helper) for session " << clientSessionId); // Removed - less verbose
      mediaSink = createNewStreamDestination(clientSessionId, streamBitrate);
      if (mediaSink == nullptr) {
          LOG_ERROR(">>> getStreamParameters: createNewStreamDestination FAILED for session " << clientSessionId);
          return;
      }
      // LOG_DEBUG("createNewStreamDestination (helper) succeeded for session " << clientSessionId << ". Sink: " << (int)mediaSink); // Removed - less verbose

      // Set up transport (UDP or TCP)
      if (clientRTPPort.num() != 0 || tcpSocketNum >= 0) {
          if (clientRTCPPort.num() == 0 && tcpSocketNum < 0) {
               // This might be okay if the client doesn't intend to send RTCP Sender Reports
               LOG_WARN("Client requested UDP streaming but provided no RTCP port for session " << clientSessionId);
           }

           if (tcpSocketNum < 0) { // UDP
               // LOG_DEBUG("Attempting UDP port allocation for session " << clientSessionId); // Removed - less verbose
               // Call the helper function to allocate ports and create groupsocks
               if (!allocateUdpPorts(serverRTPPort, serverRTCPPort, rtpGroupsock, rtcpGroupsock)) {
                   LOG_ERROR(">>> getStreamParameters: Failed to allocate UDP ports for session " << clientSessionId);
                   if (mediaSink) Medium::close(mediaSink);
                   delete rtpGroupsock; // Safe even if null
                   if (rtcpGroupsock != rtpGroupsock) delete rtcpGroupsock; // Safe even if null
                   return;
               }

           } else { // TCP
                // LOG_DEBUG("Client requested TCP interleaved mode for session " << clientSessionId); // Removed - less verbose
               serverRTPPort = 0; // Indicate TCP
               serverRTCPPort = 0;
               struct sockaddr_storage dummyAddr; memset(&dummyAddr, 0, sizeof dummyAddr); dummyAddr.ss_family = AF_INET;
               Port dummyPort(0);
               // Create dummy groupsocks - needed for RTPSource/RTCPInstance potentially
               rtpGroupsock = new Groupsock(envir(), dummyAddr, dummyPort, 0);
               rtcpGroupsock = new Groupsock(envir(), dummyAddr, dummyPort, 0);
               // LOG_DEBUG("Created dummy RTP Groupsock (" << (int)rtpGroupsock << ") and dummy RTCP Groupsock (" << (int)rtcpGroupsock << ") for TCP mode."); // Removed - less verbose
          }

          // Create the RTPSource (which receives data)
          // LOG_DEBUG("Calling createNewRTPSource (our helper) for session " << clientSessionId); // Removed - less verbose
          rtpSource = createNewRTPSource(rtpGroupsock, 0, mediaSink);
          if (rtpSource == nullptr) {
              LOG_ERROR(">>> getStreamParameters: createNewRTPSource FAILED for session " << clientSessionId);
              if (mediaSink) Medium::close(mediaSink);
              delete rtpGroupsock;
              if (rtcpGroupsock != rtpGroupsock) delete rtcpGroupsock;
              return;
          }
           // LOG_DEBUG("createNewRTPSource (helper) succeeded for session " << clientSessionId << ". Source: " << (int)rtpSource); // Removed - less verbose

      } else {
           LOG_ERROR(">>> getStreamParameters: Invalid parameters (no client ports or TCP socket) for session " << clientSessionId);
           if (mediaSink) Medium::close(mediaSink);
           return;
       }

       // Create our custom stream state object, passing destination details
       // LOG_DEBUG("Creating BackchannelStreamState for session " << clientSessionId); // Removed - less verbose
       Boolean isTCP = (tcpSocketNum >= 0);
       BackchannelStreamState* state = new BackchannelStreamState(*this, rtpSource, (BackchannelSink*)mediaSink, rtpGroupsock, rtcpGroupsock, clientSessionId,
                                                                  isTCP,
                                                                  destinationAddress, // Pass even if TCP, might be needed for RTCP handler key
                                                                  clientRTPPort,
                                                                  clientRTCPPort,
                                                                  tcpSocketNum,
                                                                  rtpChannelId,
                                                                  rtcpChannelId,
                                                                  tlsState);
       streamToken = (void*)state;
       // LOG_DEBUG("Created BackchannelStreamState for session " << clientSessionId << ". State: " << (int)state << ". Assigned to streamToken."); // Removed - less verbose

     // LOG_DEBUG("getStreamParameters complete for session " << clientSessionId << ". streamToken: " << (int)streamToken); // Removed - less verbose
  }


  void BackchannelServerMediaSubsession::startStream(unsigned clientSessionId, void* streamToken,
                               TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                               unsigned short& rtpSeqNum, unsigned& rtpTimestamp,
                               ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                               void* serverRequestAlternativeByteHandlerClientData)
  {
     // LOG_DEBUG("startStream for session " << clientSessionId << ". streamToken: " << (int)streamToken); // Removed - less verbose
     BackchannelStreamState* state = (BackchannelStreamState*)streamToken;

      if (state == nullptr) {
          LOG_ERROR("startStream called with NULL streamToken for client session " << clientSessionId);
          return;
      }

      // Call our state object's startPlaying method
      state->startPlaying(rtcpRRHandler, rtcpRRHandlerClientData,
                          serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);

      // Set initial RTP seq num and timestamp
      RTPSource* rtpSource = state->rtpSource;
      if (rtpSource != nullptr) {
          rtpSeqNum = rtpSource->curPacketMarkerBit() ? (rtpSource->curPacketRTPSeqNum() + 1) : rtpSource->curPacketRTPSeqNum();
          rtpTimestamp = 0; // Timestamp is private in RTPSource. Use 0.
          // LOG_DEBUG("startStream: Initializing rtpSeqNum=" << rtpSeqNum << ", rtpTimestamp=" << rtpTimestamp << " for session " << clientSessionId); // Removed - less verbose
      } else {
          rtpSeqNum = 0;
          rtpTimestamp = 0;
          LOG_WARN("RTPSource is NULL in state for session " << clientSessionId << ". Setting SeqNum/Timestamp to 0.");
     }
  }

  void BackchannelServerMediaSubsession::deleteStream(unsigned clientSessionId, void*& streamToken) {
      LOG_INFO("Deleting stream for session " << clientSessionId << ". streamToken: " << (int)streamToken);

      BackchannelStreamState* state = (BackchannelStreamState*)streamToken;
      if (state != nullptr) {
          // Deleting the state object triggers its destructor for cleanup
          delete state;
          streamToken = nullptr;
          LOG_INFO("Deleted BackchannelStreamState for session " << clientSessionId);
      } else {
          LOG_WARN("deleteStream called with NULL streamToken for session " << clientSessionId);
      }
      // Base class deleteStream is not called as we manage our own state object type
  }


  void BackchannelServerMediaSubsession::getRTPSinkandRTCP(void* streamToken, RTPSink*& rtpSink, RTCPInstance*& rtcp) {
      // This subsession only receives, so no RTPSink. RTCP is managed by BackchannelStreamState.
      rtpSink = nullptr;

      BackchannelStreamState* state = (BackchannelStreamState*)streamToken;
       if (state != nullptr) {
           rtcp = state->rtcpInstance; // Get RTCP instance from our state object
           // LOG_DEBUG("getRTPSinkandRTCP: Found RTCP instance " << (int)rtcp << " via BackchannelStreamState for session " << state->clientSessionId); // Removed - less verbose
       } else {
          rtcp = nullptr;
          // LOG_DEBUG("getRTPSinkandRTCP: No stream state or RTPSource found, returning NULL RTCP instance."); // Removed - less verbose
      }
  }

  void BackchannelServerMediaSubsession::closeStreamSource(FramedSource *inputSource) {
       // LOG_DEBUG("Closing stream source (FramedSource): " << (int)inputSource); // Removed - less verbose
       Medium::close(inputSource);
  }

  FramedSource* BackchannelServerMediaSubsession::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
      // This subsession receives, it doesn't provide a source to an RTPSink.
      // LOG_DEBUG("createNewStreamSource called for session " << clientSessionId); // Removed - less verbose
      if (fFormat == IMPBackchannelFormat::OPUS) {
           estBitrate = 32;
      } else {
           estBitrate = 64;
      }
      // LOG_DEBUG("Estimated bitrate for session " << clientSessionId << ": " << estBitrate); // Removed - less verbose
      return nullptr;
  }

  RTPSink* BackchannelServerMediaSubsession::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource) {
      // This subsession receives, it doesn't create an RTPSink for sending.
      // LOG_DEBUG("createNewRTPSink called (rtpGroupsock: " << (int)rtpGroupsock << ", payloadType: " << (int)rtpPayloadTypeIfDynamic << ")"); // Removed - less verbose
      return nullptr;
  }

  Boolean BackchannelServerMediaSubsession::allocateUdpPorts(Port& serverRTPPort, Port& serverRTCPPort, Groupsock*& rtpGroupsock, Groupsock*& rtcpGroupsock) {
      NoReuse dummy(envir());
      portNumBits serverPortNum = fInitialPortNum;

      while(1) {
          serverRTPPort = serverPortNum;
          struct sockaddr_storage nullAddr;
          memset(&nullAddr, 0, sizeof(nullAddr));
          nullAddr.ss_family = AF_INET;
          rtpGroupsock = createGroupsock(nullAddr, serverRTPPort);
          if (rtpGroupsock->socketNum() < 0) {
              delete rtpGroupsock; rtpGroupsock = nullptr;
              serverPortNum += (fMultiplexRTCPWithRTP ? 1 : 2);
              if (serverPortNum == 0) return False; // Port wrap-around check
              continue;
          }

          if (fMultiplexRTCPWithRTP) {
              serverRTCPPort = serverRTPPort;
              rtcpGroupsock = rtpGroupsock;
          } else {
              serverRTCPPort = ++serverPortNum;
              if (serverPortNum == 0) { // Port wrap-around check
                  delete rtpGroupsock; rtpGroupsock = nullptr;
                  return False;
              }
              rtcpGroupsock = createGroupsock(nullAddr, serverRTCPPort);
              if (rtcpGroupsock->socketNum() < 0) {
                  delete rtpGroupsock; rtpGroupsock = nullptr;
                  delete rtcpGroupsock; rtcpGroupsock = nullptr;
                  ++serverPortNum; // Port pair failed, try next
                  if (serverPortNum == 0) return False; // Port wrap-around check
                  continue;
              }
           }
           LOG_DEBUG("UDP port allocation succeeded. RTP=" << ntohs(serverRTPPort.num()) << ", RTCP=" << ntohs(serverRTCPPort.num()));
           return True; // Success
       }
  }
