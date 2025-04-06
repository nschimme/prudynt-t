#include "BackchannelStreamState.hpp"
#include "BackchannelServerMediaSubsession.hpp"
#include "BackchannelSink.hpp"
#include "Logger.hpp"
#include <liveMedia.hh>
#include <cstring>

#define MODULE "BackchannelStreamState"

BackchannelStreamState::BackchannelStreamState(BackchannelServerMediaSubsession& _master,
                                               RTPSource* _rtpSource, BackchannelSink* _mediaSink,
                                               Groupsock* _rtpGS, Groupsock* _rtcpGS, unsigned _clientSessionId)
    : master(_master), rtpSource(_rtpSource), mediaSink(_mediaSink),
      rtpGS(_rtpGS), rtcpGS(_rtcpGS), rtcpInstance(nullptr), clientSessionId(_clientSessionId),
      fReferenceCount(1)
{
    LOG_DEBUG("BackchannelStreamState created for client session " << clientSessionId);
}

BackchannelStreamState::~BackchannelStreamState()
{
    LOG_DEBUG("BackchannelStreamState destroyed for client session " << clientSessionId);
    Medium::close(rtcpInstance);

    if (mediaSink && rtpSource) {
        mediaSink->stopPlaying();
    }

    master.closeStreamSource(rtpSource);
    master.closeStreamSink(mediaSink);

    delete rtpGS;
    if (rtcpGS != nullptr && rtcpGS != rtpGS) {
        delete rtcpGS;
    }
}

void BackchannelStreamState::startPlaying(BackchannelDestinations* dests, TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                                          ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                                          void* serverRequestAlternativeByteHandlerClientData)
{
    if (mediaSink && rtpSource && dests) {
        rtcpInstance = RTCPInstance::createNew(master.envir(), rtcpGS,
                                               64 /*est BW kbps*/, (unsigned char*)master.fCNAME,
                                               nullptr /*sink*/, rtpSource /*source*/,
                                               True /*is server*/);
        if (rtcpInstance) {
             rtcpInstance->setRRHandler(rtcpRRHandler, rtcpRRHandlerClientData);

             if (dests->isTCP) {
                 LOG_DEBUG("Configuring stream for TCP interleaved mode (socket " << dests->tcpSocketNum << ", RTP ch " << (int)dests->rtpChannelId << ", RTCP ch " << (int)dests->rtcpChannelId << ")");
                 rtpSource->setStreamSocket(dests->tcpSocketNum, dests->rtpChannelId, dests->tlsState);
                 rtcpInstance->addStreamSocket(dests->tcpSocketNum, dests->rtcpChannelId, dests->tlsState);

                 RTPInterface::setServerRequestAlternativeByteHandler(master.envir(), dests->tcpSocketNum,
                                                                      serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);

                 struct sockaddr_storage tcpSocketNumAsAddress;
                 memset(&tcpSocketNumAsAddress, 0, sizeof(tcpSocketNumAsAddress));
                 tcpSocketNumAsAddress.ss_family = AF_INET;
                 ((struct sockaddr_in&)tcpSocketNumAsAddress).sin_addr.s_addr = dests->tcpSocketNum;
                 rtcpInstance->setSpecificRRHandler(tcpSocketNumAsAddress, dests->rtcpChannelId,
                                                    rtcpRRHandler, rtcpRRHandlerClientData);

             } else {
                 LOG_DEBUG("Configuring stream for UDP mode");
                 if (rtpGS) rtpGS->addDestination(dests->addr, dests->rtpPort, clientSessionId);
                 if (rtcpGS) rtcpGS->addDestination(dests->addr, dests->rtcpPort, clientSessionId);
                 rtcpInstance->setSpecificRRHandler(dests->addr, dests->rtcpPort,
                                                    rtcpRRHandler, rtcpRRHandlerClientData);
             }

              rtcpInstance->sendReport();

          } else {
              LOG_WARN("Failed to create RTCPInstance for client session " << clientSessionId);
      }

      LOG_INFO("Connecting BackchannelSink directly to RTPSource for client session " << clientSessionId);
      if (!mediaSink->startPlaying(*rtpSource, nullptr /*afterPlayingFunc*/, this /*unused clientData*/)) {
          LOG_ERROR("mediaSink->startPlaying failed (direct connection) for client session " << clientSessionId);
          Medium::close(rtcpInstance); rtcpInstance = nullptr;
     } else {
          LOG_INFO("Connected BackchannelSink directly to RTPSource for client session " << clientSessionId);
     }
     } else {
        if (!mediaSink) LOG_ERROR("Cannot start playing - mediaSink is NULL for client session " << clientSessionId);
        else if (!rtpSource) LOG_ERROR("Cannot start playing - rtpSource is NULL for client session " << clientSessionId);
        else if (!dests) LOG_ERROR("Cannot start playing - dests is NULL for client session " << clientSessionId);
        else LOG_ERROR("Cannot start playing - unknown reason for client session " << clientSessionId);
    }
}
