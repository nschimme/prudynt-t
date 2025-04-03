#include "CustomRTSPServer.hpp"

CustomRTSPServer* CustomRTSPServer::createNew(UsageEnvironment& env, Port ourPort,
                                            UserAuthenticationDatabase* authDatabase,
                                            unsigned reclamationTestSeconds) {
    // Open the TCP socket(s) on which we'll listen for client connections:
    int ourSocket = setUpOurSocket(env, ourPort);
    if (ourSocket == -1) return NULL;

    return new CustomRTSPServer(env, ourSocket, ourPort, authDatabase, reclamationTestSeconds);
}

CustomRTSPServer::CustomRTSPServer(UsageEnvironment& env, int ourSocket, Port ourPort,
                                   UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds)
    : RTSPServer(env, ourSocket, ourPort, authDatabase, reclamationTestSeconds) {
}

CustomRTSPServer::~CustomRTSPServer() {
}

RTSPClientSession* CustomRTSPServer::createNewClientSession(unsigned sessionId, struct sockaddr_storage const& clientAddr) {
    // Create and return an instance of our custom client session class:
    return new CustomRTSPClientSession(*this, sessionId, clientAddr, fAuthDB);
}
