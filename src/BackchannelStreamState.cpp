#include "BackchannelStreamState.hpp"
#include "BackchannelServerMediaSubsession.hpp" // Needed for master definition and fCNAME
#include "BackchannelSink.hpp"                 // Needed for mediaSink definition
#include "RTPPacketValidatorFilter.hpp"        // Include the new filter header
#include "Logger.hpp"                          // For logging
#include <liveMedia.hh>                         // For Live555 classes like Medium, RTCPInstance
#include <cstring>                              // For memset

#define MODULE "BackchannelStreamState" // Optional: Define module for logging

BackchannelStreamState::BackchannelStreamState(BackchannelServerMediaSubsession& _master,
                                               RTPSource* _rtpSource, BackchannelSink* _mediaSink,
                                               Groupsock* _rtpGS, Groupsock* _rtcpGS, unsigned _clientSessionId)
    : master(_master), rtpSource(_rtpSource), mediaSink(_mediaSink),
      rtpGS(_rtpGS), rtcpGS(_rtcpGS), rtcpInstance(nullptr), clientSessionId(_clientSessionId),
      fReferenceCount(1) // Initialize reference count to 1
{
    // Constructor body (if needed, currently empty)
    LOG_DEBUG("BackchannelStreamState created for client session " << clientSessionId);
}

BackchannelStreamState::~BackchannelStreamState()
{
    LOG_DEBUG("BackchannelStreamState destroyed for client session " << clientSessionId);
    // Cleanup RTCP first (sends BYE)
    Medium::close(rtcpInstance);

    // Stop the sink before closing the source
    if (mediaSink && rtpSource) {
        mediaSink->stopPlaying(); // Ensure sink stops using source
    }

    // Close source and sink using the public methods on master
    // Note: These calls might be redundant if Medium::close handles them,
    // but explicitly calling them ensures our class logic is followed.
    master.closeStreamSource(rtpSource);
    master.closeStreamSink(mediaSink);

    // Delete groupsocks
    // Note: In TCP mode, rtpGS might be the dummy groupsock created in getStreamParameters,
    // and rtcpGS will be NULL. In UDP mode, both should be valid (or the same if multiplexed).
    delete rtpGS; // Safe to delete NULL or the dummy/real UDP groupsock
    if (rtcpGS != nullptr && rtcpGS != rtpGS) { // Only delete rtcpGS if it's non-NULL and different from rtpGS
        delete rtcpGS;
    }
}

// Updated signature to accept BackchannelDestinations and byte handler
void BackchannelStreamState::startPlaying(BackchannelDestinations* dests, TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                                          ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                                          void* serverRequestAlternativeByteHandlerClientData)
{
    if (mediaSink && rtpSource && dests) { // Check dests as well
        // Create RTCP instance (we are receiving, so RTPSource is non-NULL)
        rtcpInstance = RTCPInstance::createNew(master.envir(), rtcpGS,
                                               64 /*est BW kbps*/, (unsigned char*)master.fCNAME, // Use master's CNAME
                                               nullptr /*sink*/, rtpSource /*source*/,
                                               True /*is server*/);
        if (rtcpInstance) {
             rtcpInstance->setRRHandler(rtcpRRHandler, rtcpRRHandlerClientData);
             // Optional: Send initial RR? Might not be needed for recvonly.

             // Configure RTCP/RTP based on transport (UDP or TCP)
             if (dests->isTCP) {
                 // TCP Interleaved
                 LOG_DEBUG("Configuring stream for TCP interleaved mode (socket " << dests->tcpSocketNum << ", RTP ch " << (int)dests->rtpChannelId << ", RTCP ch " << (int)dests->rtcpChannelId << ")");
                 rtpSource->setStreamSocket(dests->tcpSocketNum, dests->rtpChannelId, dests->tlsState);
                 rtcpInstance->addStreamSocket(dests->tcpSocketNum, dests->rtcpChannelId, dests->tlsState);

                 // Allow RTSP commands to continue on the same socket
                 RTPInterface::setServerRequestAlternativeByteHandler(master.envir(), dests->tcpSocketNum,
                                                                      serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);

                 // Set RTCP RR handler for TCP connection (using socket num as address hack)
                 struct sockaddr_storage tcpSocketNumAsAddress;
                 memset(&tcpSocketNumAsAddress, 0, sizeof(tcpSocketNumAsAddress));
                 tcpSocketNumAsAddress.ss_family = AF_INET; // Needs a family
                 ((struct sockaddr_in&)tcpSocketNumAsAddress).sin_addr.s_addr = dests->tcpSocketNum; // Store socket num
                 rtcpInstance->setSpecificRRHandler(tcpSocketNumAsAddress, dests->rtcpChannelId,
                                                    rtcpRRHandler, rtcpRRHandlerClientData);

             } else {
                 // UDP
                 LOG_DEBUG("Configuring stream for UDP mode");
                 if (rtpGS) rtpGS->addDestination(dests->addr, dests->rtpPort, clientSessionId); // Add client as destination for RTP (even though we receive?) - Maybe not needed? Check Live555 examples for recvonly server. Let's assume it's needed for RTCP pairing.
                 if (rtcpGS) rtcpGS->addDestination(dests->addr, dests->rtcpPort, clientSessionId); // Add client as destination for RTCP reports
                 rtcpInstance->setSpecificRRHandler(dests->addr, dests->rtcpPort,
                                                    rtcpRRHandler, rtcpRRHandlerClientData);
             }

             // Hack: Send initial RTCP packet? (From example, might help sync)
             rtcpInstance->sendReport();

         } else {
             LOG_WARN("Failed to create RTCPInstance for client session " << clientSessionId);
     }

     // Create the validator filter, using the RTPSource as its input
     RTPPacketValidatorFilter* validatorFilter = RTPPacketValidatorFilter::createNew(master.envir(), rtpSource);
     if (!validatorFilter) {
         LOG_ERROR("Failed to create RTPPacketValidatorFilter for client session " << clientSessionId);
         // Cleanup RTCP instance if created
         Medium::close(rtcpInstance); rtcpInstance = nullptr;
         return;
     }
     LOG_INFO("Created RTPPacketValidatorFilter for client session " << clientSessionId);

     // Connect the validator filter to the sink (start consuming data)
     // The sink now receives data from the filter, not directly from the source.
     if (!mediaSink->startPlaying(*validatorFilter, nullptr /*afterPlayingFunc*/, this /*unused clientData*/)) {
         LOG_ERROR("mediaSink->startPlaying failed (with filter) for client session " << clientSessionId);
         // Handle error? Maybe close stream? Close the filter?
         Medium::close(validatorFilter);
    } else {
         LOG_INFO("Connected RTPPacketValidatorFilter to BackchannelSink for client session " << clientSessionId);
    }
    } else {
        // Check which pointer is null for better logging
        if (!mediaSink) LOG_ERROR("Cannot start playing - mediaSink is NULL for client session " << clientSessionId);
        else if (!rtpSource) LOG_ERROR("Cannot start playing - rtpSource is NULL for client session " << clientSessionId);
        else if (!dests) LOG_ERROR("Cannot start playing - dests is NULL for client session " << clientSessionId);
        else LOG_ERROR("Cannot start playing - unknown reason for client session " << clientSessionId); // Should not happen
    }
}
