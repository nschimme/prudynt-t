#include "BackchannelStreamState.hpp"
#include "BackchannelServerMediaSubsession.hpp"
#include "BackchannelSink.hpp"
#include "Logger.hpp"
#include <liveMedia.hh>
#include <cstring>

#define MODULE "BackchannelStreamState"

 // Updated constructor implementation
 BackchannelStreamState::BackchannelStreamState(BackchannelServerMediaSubsession& _master,
                                                RTPSource* _rtpSource, BackchannelSink* _mediaSink,
                                                Groupsock* _rtpGS, Groupsock* _rtcpGS, unsigned _clientSessionId,
                                                // Destination parameters:
                                                Boolean _isTCP,
                                                struct sockaddr_storage const& _destAddr,
                                                Port const& _rtpDestPort,
                                                Port const& _rtcpDestPort,
                                                int _tcpSocketNum,
                                                unsigned char _rtpChannelId,
                                                unsigned char _rtcpChannelId,
                                                TLSState* _tlsState)
      : master(_master), rtpSource(_rtpSource), mediaSink(_mediaSink),
        rtpGS(_rtpGS), rtcpGS(_rtcpGS), rtcpInstance(nullptr), clientSessionId(_clientSessionId),
        // Initialize transport flag
        fIsTCP(_isTCP)
        // fTransport union is initialized below based on fIsTCP
  {
      LOG_DEBUG("Created for session " << clientSessionId << " (TCP: " << fIsTCP << ")");
      // Initialize the appropriate part of the union
      if (fIsTCP) {
          fTransport.t.tcpSocketNum = _tcpSocketNum;
          fTransport.t.rtpChannelId = _rtpChannelId;
          fTransport.t.rtcpChannelId = _rtcpChannelId;
          fTransport.t.tlsState = _tlsState;
      } else {
          fTransport.u.destAddr = _destAddr;
          fTransport.u.rtpDestPort = _rtpDestPort;
          fTransport.u.rtcpDestPort = _rtcpDestPort;
      }
  }

BackchannelStreamState::~BackchannelStreamState()
{
    LOG_DEBUG("Destroyed for session " << clientSessionId);
    Medium::close(rtcpInstance);

    if (mediaSink && rtpSource) {
        mediaSink->stopPlaying();
     }

     master.closeStreamSource(rtpSource); // Close source via master
     Medium::close(mediaSink); // Close sink directly using Medium::close

     delete rtpGS;
    if (rtcpGS != nullptr && rtcpGS != rtpGS) {
        delete rtcpGS;
     }
 }

 // Updated startPlaying implementation - uses internal state (fIsTCP, fDestAddr, etc.)
 void BackchannelStreamState::startPlaying(TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                                           ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                                           void* serverRequestAlternativeByteHandlerClientData)
 {
     // Check if sink and source are valid before proceeding
     if (mediaSink && rtpSource) {
         // Create RTCP instance (using stored rtcpGS)
         rtcpInstance = RTCPInstance::createNew(master.envir(), rtcpGS,
                                                64 /*est BW kbps*/, (unsigned char*)master.fCNAME, // Use CNAME from master
                                                nullptr /*sink*/, rtpSource /*source*/,
                                                True /*is server*/);
         if (rtcpInstance) {
              // Set the general RR handler
              rtcpInstance->setRRHandler(rtcpRRHandler, rtcpRRHandlerClientData);

               // Configure transport based on stored fIsTCP flag and union data
               if (fIsTCP) {
                   LOG_DEBUG("Configuring stream for TCP (socket " << fTransport.t.tcpSocketNum << ", RTP ch " << (int)fTransport.t.rtpChannelId << ", RTCP ch " << (int)fTransport.t.rtcpChannelId << ")");
                   // Set socket and channel IDs for RTP source and RTCP instance
                   rtpSource->setStreamSocket(fTransport.t.tcpSocketNum, fTransport.t.rtpChannelId, fTransport.t.tlsState);
                   rtcpInstance->addStreamSocket(fTransport.t.tcpSocketNum, fTransport.t.rtcpChannelId, fTransport.t.tlsState);

                   // Set the alternative byte handler for TCP
                   RTPInterface::setServerRequestAlternativeByteHandler(master.envir(), fTransport.t.tcpSocketNum,
                                                                        serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);

                   // Set specific RR handler for TCP (using socket num as address identifier)
                   struct sockaddr_storage tcpSocketNumAsAddress;
                   memset(&tcpSocketNumAsAddress, 0, sizeof(tcpSocketNumAsAddress));
                   tcpSocketNumAsAddress.ss_family = AF_INET; // Needs to be a valid family
                   ((struct sockaddr_in&)tcpSocketNumAsAddress).sin_addr.s_addr = htonl(fTransport.t.tcpSocketNum); // Use socket num (ensure network byte order?)
                   rtcpInstance->setSpecificRRHandler(tcpSocketNumAsAddress, fTransport.t.rtcpChannelId,
                                                      rtcpRRHandler, rtcpRRHandlerClientData);

               } else { // UDP
                   LOG_DEBUG("Configuring stream for UDP");
                   // Add destinations to groupsocks using stored address/ports from union
                   if (rtpGS) rtpGS->addDestination(fTransport.u.destAddr, fTransport.u.rtpDestPort, clientSessionId);
                   if (rtcpGS) rtcpGS->addDestination(fTransport.u.destAddr, fTransport.u.rtcpDestPort, clientSessionId);
                   // Set specific RR handler for UDP using address/port from union
                   rtcpInstance->setSpecificRRHandler(fTransport.u.destAddr, fTransport.u.rtcpDestPort,
                                                      rtcpRRHandler, rtcpRRHandlerClientData);
               }

                // Send initial RTCP report
               rtcpInstance->sendReport();

           } else {
               // Failed to create RTCP instance
               LOG_WARN("Failed to create RTCPInstance for session " << clientSessionId);
       }

       // Connect the sink to the source to start data flow
       LOG_INFO("Connecting Sink to Source for session " << clientSessionId);
       if (!mediaSink->startPlaying(*rtpSource, nullptr /*afterPlayingFunc*/, this /*unused clientData*/)) {
           // Failed to connect sink/source
           LOG_ERROR("mediaSink->startPlaying failed (direct connection) for client session " << clientSessionId);
           Medium::close(rtcpInstance); rtcpInstance = nullptr; // Clean up RTCP if created
      } else {
           // Successfully connected
           LOG_INFO("Connected Sink to Source for session " << clientSessionId);
      }
      } else {
         // Log error if sink or source is missing
         if (!mediaSink) LOG_ERROR("Cannot start playing - mediaSink is NULL for client session " << clientSessionId);
         else if (!rtpSource) LOG_ERROR("Cannot start playing - rtpSource is NULL for client session " << clientSessionId);
         else LOG_ERROR("Cannot start playing - unknown reason for client session " << clientSessionId);
     }
 }
