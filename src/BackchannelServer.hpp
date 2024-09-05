#ifndef BACKCHANNELSERVER_HPP
#define BACKCHANNELSERVER_HPP

#include <liveMedia/RTSPServer.hh>

class BackchannelServer : public RTSPServer {
public:
    static BackchannelServer* createNew(UsageEnvironment& env, Port port);

protected:
    BackchannelServer(UsageEnvironment& env, Port port);

    virtual void handleCmd_SETUP(RTSPClientConnection* ourClientConnection,
                                 char const* urlPreSuffix, char const* urlSuffix,
                                 char const* fullRequestStr) override;

    virtual void handleCmd_DESCRIBE(RTSPClientConnection* ourClientConnection,
                                    char const* urlPreSuffix, char const* urlSuffix,
                                    char const* fullRequestStr) override;

    // Other methods and members for handling Profile-T features

private:
    // Private methods and members for internal logic
};

#endif // BACKCHANNELSERVER_HPP
