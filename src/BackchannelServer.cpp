#include "BackchannelServer.hpp"
#include "BackchannelAudioSubsession.hpp"
#include <iostream>

BackchannelServer* BackchannelServer::createNew(UsageEnvironment& env, Port ourPort,
                                                UserAuthenticationDatabase* authDatabase,
                                                unsigned reclamationTestSeconds)
{
    return new BackchannelServer(env, ourPort, reclamationTestSeconds);
}

BackchannelServer::BackchannelServer(UsageEnvironment& env, Port port, unsigned reclamationTestSeconds)
    : RTSPServer(env, port, reclamationTestSeconds)
{}

void BackchannelServer::handleCmd_SETUP(RTSPClientConnection* ourClientConnection,
                                        char const* urlPreSuffix, char const* urlSuffix,
                                        char const* fullRequestStr) {
    // Lookup the media session and subsession
    ServerMediaSession* sms = lookupServerMediaSession(urlSuffix);
    if (sms) {
        BackchannelAudioSubsession* subsession = dynamic_cast<BackchannelAudioSubsession*>(sms->lookupSubsession(urlSuffix));
        if (subsession) {
            // Get SDP from client and negotiate codec
            char const* sdpLines = ourClientConnection->getClientInputSDP();
            negotiateProfileTCodec(ourClientConnection, sdpLines, subsession);
            // Store the subsession associated with the client
            subsessionMap_[ourClientConnection] = subsession;
        }
    }
    // Proceed with the normal RTSP setup
    RTSPServer::handleCmd_SETUP(ourClientConnection, urlPreSuffix, urlSuffix, fullRequestStr);
}

void BackchannelServer::negotiateProfileTCodec(RTSPClientConnection* ourClientConnection,
                                               char const* sdpLines, BackchannelAudioSubsession* subsession) {
    // Simple parsing to extract codec information
    std::string sdpStr(sdpLines);
    if (sdpStr.find("PCMA") != std::string::npos) {
        subsession->setCodec("PCMA");
    } else if (sdpStr.find("PCMU") != std::string::npos) {
        subsession->setCodec("PCMU");
    } else if (sdpStr.find("AAC") != std::string::npos) {
        subsession->setCodec("AAC");
    } else {
        std::cerr << "Unsupported codec, defaulting to PCMU\n";
        subsession->setCodec("PCMU");
    }
}

