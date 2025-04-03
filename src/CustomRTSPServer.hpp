#ifndef CUSTOM_RTSP_SERVER_HPP
#define CUSTOM_RTSP_SERVER_HPP

#include "RTSPServer.hh"
#include "CustomRTSPClientSession.hpp" // Include our custom session

class CustomRTSPServer : public RTSPServer {
public:
    // Factory method
    static CustomRTSPServer* createNew(UsageEnvironment& env, Port ourPort,
                                       UserAuthenticationDatabase* authDatabase = NULL,
                                       unsigned reclamationTestSeconds = 65);

protected:
    CustomRTSPServer(UsageEnvironment& env, int ourSocket, Port ourPort,
                     UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds);
    // called only by createNew();
    virtual ~CustomRTSPServer();

    // Override the key virtual function:
    virtual RTSPClientSession* createNewClientSession(unsigned sessionId, struct sockaddr_storage const& clientAddr);
};

#endif // CUSTOM_RTSP_SERVER_HPP
