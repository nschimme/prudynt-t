#ifndef CUSTOM_RTSP_CLIENT_SESSION_HPP
#define CUSTOM_RTSP_CLIENT_SESSION_HPP

#include "RTSPServer.hh"
#include "ServerMediaSession.hh"
#include "BackchannelSink.hpp" // Include the sink we created
#include "Logger.hpp"

// Define a constant for the backchannel target IP and port
#define BACKCHANNEL_TARGET_IP "127.0.0.1"
#define BACKCHANNEL_TARGET_PORT 8081
#define BACKCHANNEL_PAYLOAD_TYPE 8 // PCMA (G.711 A-law)

class CustomRTSPClientSession : public RTSPServer::RTSPClientSession {
public:
    CustomRTSPClientSession(RTSPServer& ourServer, unsigned sessionId,
                            struct sockaddr_storage const& clientAddr,
                            Authenticator* authenticator);
    virtual ~CustomRTSPClientSession();

protected:
    // Override RTSP command handlers:
    virtual void handleCmd_SETUP(RTSPClientConnection* ourClientConnection,
                                 char const* urlPreSuffix, char const* urlSuffix,
                                 char const* fullRequestStr);

    virtual void handleCmd_TEARDOWN(RTSPClientConnection* ourClientConnection,
                                    ServerMediaSubsession* subsession);

private:
    // Track the backchannel sink associated with this session (if any)
    // We might need a map if multiple backchannels per session are possible,
    // but for one audio backchannel, a single pointer might suffice.
    // Let's assume one sink per session for now.
    BackchannelSink* fBackchannelSink;
    ServerMediaSubsession* fBackchannelSubsession; // Keep track of which subsession is the backchannel
};

#endif // CUSTOM_RTSP_CLIENT_SESSION_HPP
