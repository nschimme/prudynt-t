#include "BackchannelServerMediaSubsession.hpp"
#include "BackchannelSink.hpp"
#include "Logger.hpp"
#include "globals.hpp"
#include "IMPBackchannel.hpp"

#include <NetAddress.hh>
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <SimpleRTPSource.hh>
#include <HashTable.hh>
#include "BackchannelStreamState.hpp"

#include <cassert>
#include <unistd.h>
#include <cstring>
#include <sys/time.h>

#define MODULE "BackchannelSubsession"

static void delayedDeleteTask(void* clientData) {
    BackchannelStreamState* state = (BackchannelStreamState*)clientData;
    LOG_WARN(">>> delayedDeleteTask: Deleting BackchannelStreamState");
    delete state;
}


BackchannelServerMediaSubsession* BackchannelServerMediaSubsession::createNew(UsageEnvironment& env, backchannel_stream* stream_data, Boolean reuseFirstSource) {
    return new BackchannelServerMediaSubsession(env, stream_data, reuseFirstSource);
}

BackchannelServerMediaSubsession::BackchannelServerMediaSubsession(UsageEnvironment& env, backchannel_stream* stream_data, Boolean reuseFirstSource)
    : ServerMediaSubsession(env), fSDPLines(nullptr), fStreamData(stream_data),
      fLastStreamToken(nullptr), fReuseFirstSource(reuseFirstSource),
      fInitialPortNum(6970), fMultiplexRTCPWithRTP(False)
{
    LOG_DEBUG("BackchannelServerMediaSubsession created (using ServerMediaSubsession base)");
    fDestinationsHashTable = HashTable::create(ONE_WORD_HASH_KEYS);
    if (fStreamData == nullptr) {
        LOG_WARN("backchannel_stream data provided is null!");
    }
    gethostname(fCNAME, MAX_CNAME_LEN);
    fCNAME[MAX_CNAME_LEN] = '\0';

    if (!fMultiplexRTCPWithRTP) {
        fInitialPortNum = (fInitialPortNum + 1) & ~1;
    }
}

BackchannelServerMediaSubsession::~BackchannelServerMediaSubsession() {
    LOG_DEBUG("BackchannelServerMediaSubsession destroyed");
    delete[] fSDPLines;

    while (true) {
        BackchannelDestinations* destinations = (BackchannelDestinations*)(fDestinationsHashTable->RemoveNext());
        if (destinations == nullptr) break;
        LOG_DEBUG("Deleting BackchannelDestinations object");
        delete destinations;
    }
    delete fDestinationsHashTable;
}

char const* BackchannelServerMediaSubsession::sdpLines(int /*addressFamily*/) {
    if (fSDPLines == nullptr) {
        LOG_DEBUG("Generating SDP lines for backchannel (addressFamily ignored)");

        unsigned int sdpLinesSize = 300;
        fSDPLines = new char[sdpLinesSize];
        if (fSDPLines == nullptr) return nullptr;

        snprintf(fSDPLines, sdpLinesSize,
                 "m=audio 0 RTP/AVP %d\r\n"
                 "c=IN IP4 0.0.0.0\r\n"
                 "b=AS:64\r\n"
                  "a=rtpmap:%d PCMA/%u/1\r\n"
                  "a=control:%s\r\n"
                  "a=sendonly\r\n",
                  IMPBackchannel::rtpPayloadTypeFromFormat(IMPBackchannelFormat::PCMA),
                  IMPBackchannel::rtpPayloadTypeFromFormat(IMPBackchannelFormat::PCMA), IMPBackchannel::getFrequency(IMPBackchannelFormat::PCMA),
                  trackId()
         );

        fSDPLines[sdpLinesSize - 1] = '\0';
    }
    return fSDPLines;
}

MediaSink* BackchannelServerMediaSubsession::createNewStreamDestination(unsigned clientSessionId, unsigned& estBitrate) {
    estBitrate = 64;
    LOG_DEBUG("Creating new BackchannelSink for client session " << clientSessionId);
    IMPBackchannelFormat format = IMPBackchannelFormat::PCMA;
    return BackchannelSink::createNew(envir(), fStreamData, clientSessionId, format);
}

RTPSource* BackchannelServerMediaSubsession::createNewRTPSource(Groupsock* rtpGroupsock, unsigned char /*rtpPayloadTypeIfDynamic*/, MediaSink* /*outputSink*/) {
    LOG_DEBUG("Creating new SimpleRTPSource for backchannel");
     return SimpleRTPSource::createNew(envir(), rtpGroupsock,
                                       IMPBackchannel::rtpPayloadTypeFromFormat(IMPBackchannelFormat::PCMA),
                                       IMPBackchannel::getFrequency(IMPBackchannelFormat::PCMA),
                                       "audio/PCMA",
                                       0,
                                       False);
}

Boolean BackchannelServerMediaSubsession::getServerPorts(Port& rtpPort, Port& rtcpPort) {
    if (fLastStreamToken == nullptr) return False;
    BackchannelStreamState* lastState = (BackchannelStreamState*)fLastStreamToken;
    if (lastState->rtpGS == nullptr || lastState->rtcpGS == nullptr) {
         LOG_WARN("getServerPorts: Cannot reuse state as it lacks UDP groupsocks (likely TCP state).");
         return False;
    }
    rtpPort = lastState->rtpGS->port();
    rtcpPort = lastState->rtcpGS->port();
    return True;
}

char const* BackchannelServerMediaSubsession::getAuxSDPLine(RTPSink* /*rtpSink*/, FramedSource* /*inputSource*/) {
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
    LOG_INFO(">>> getStreamParameters RAW INPUT for session " << clientSessionId
             << ": clientRTPPort=" << ntohs(clientRTPPort.num())
             << ", clientRTCPPort=" << ntohs(clientRTCPPort.num())
             << ", tcpSocketNum=" << tcpSocketNum);

    LOG_WARN(">>> getStreamParameters ENTERED for session " << clientSessionId);
    isMulticast = False;
    streamToken = nullptr;

    if (addressIsNull(destinationAddress)) {
        destinationAddress = clientAddress;
    }

    if (fLastStreamToken != nullptr && fReuseFirstSource) {
        LOG_INFO("Reusing existing stream state for client session " << clientSessionId);
        BackchannelStreamState* lastState = (BackchannelStreamState*)fLastStreamToken;
        if (lastState->rtpGS == nullptr || lastState->rtcpGS == nullptr) {
             LOG_ERROR("Failed to get server ports from reusable stream state (not UDP?)!");
             return;
        }
        serverRTPPort = lastState->rtpGS->port();
        serverRTCPPort = lastState->rtcpGS->port();
        lastState->incrementReferenceCount();
        streamToken = fLastStreamToken;
        LOG_DEBUG("getStreamParameters (reused state): Ref count now " << lastState->referenceCount());
    } else {
        LOG_WARN(">>> getStreamParameters: Not reusing state for session " << clientSessionId << ". Proceeding to create new state.");
        unsigned streamBitrate = 0;
        MediaSink* mediaSink = nullptr;
        RTPSource* rtpSource = nullptr;
        Groupsock* rtpGroupsock = nullptr;
        Groupsock* rtcpGroupsock = nullptr;

        LOG_WARN(">>> getStreamParameters: Calling createNewStreamDestination for session " << clientSessionId);
        mediaSink = createNewStreamDestination(clientSessionId, streamBitrate);
        if (mediaSink == nullptr) {
            LOG_ERROR(">>> getStreamParameters: createNewStreamDestination FAILED for session " << clientSessionId);
            return;
        }
        LOG_WARN(">>> getStreamParameters: createNewStreamDestination SUCCEEDED for session " << clientSessionId << ". Sink: " << (int)mediaSink);

        if (clientRTPPort.num() != 0 || tcpSocketNum >= 0) {
            if (clientRTCPPort.num() == 0 && tcpSocketNum < 0) {
                 LOG_ERROR("Raw UDP streaming not supported for backchannel");
                 if (mediaSink) Medium::close(mediaSink);
                 return;
            } else if (tcpSocketNum < 0) {
                LOG_WARN(">>> getStreamParameters: Attempting UDP port allocation for session " << clientSessionId);
                NoReuse dummy(envir());
                portNumBits serverPortNum = fInitialPortNum;
                while(1) {
                    serverRTPPort = serverPortNum;
                    struct sockaddr_storage nullAddr;
                    memset(&nullAddr, 0, sizeof(nullAddr));
                    nullAddr.ss_family = AF_INET;
                    rtpGroupsock = new Groupsock(envir(), nullAddr, serverRTPPort, 255);
                    if (rtpGroupsock->socketNum() < 0) {
                        delete rtpGroupsock; rtpGroupsock = nullptr;
                        serverPortNum += (fMultiplexRTCPWithRTP ? 1 : 2);
                        continue;
                    }

                    if (fMultiplexRTCPWithRTP) {
                        serverRTCPPort = serverRTPPort;
                        rtcpGroupsock = rtpGroupsock;
                    } else {
                        serverRTCPPort = ++serverPortNum;
                        rtcpGroupsock = new Groupsock(envir(), nullAddr, serverRTCPPort, 255);
                        if (rtcpGroupsock->socketNum() < 0) {
                            delete rtpGroupsock; rtpGroupsock = nullptr;
                            delete rtcpGroupsock; rtcpGroupsock = nullptr;
                            ++serverPortNum;
                            continue;
                        }
                     }
                     break;
                 }
                 if (rtpGroupsock != nullptr && rtpGroupsock->socketNum() >= 0) {
                     unsigned requestedSize = 262144;
                     LOG_INFO(">>> Attempting to increase RTP socket receive buffer for session " << clientSessionId << " to " << requestedSize);
                     increaseReceiveBufferTo(envir(), rtpGroupsock->socketNum(), requestedSize);
                 }
                 LOG_WARN(">>> getStreamParameters: UDP port allocation SUCCEEDED for session " << clientSessionId << ". RTP=" << ntohs(serverRTPPort.num()) << ", RTCP=" << ntohs(serverRTCPPort.num()));
             } else {
                  LOG_WARN(">>> getStreamParameters: Client requested TCP interleaved mode for session " << clientSessionId);
                 serverRTPPort = 0;
                 serverRTCPPort = 0;
                 rtpGroupsock = nullptr;
                 rtcpGroupsock = nullptr;
                 struct sockaddr_storage dummyAddr; memset(&dummyAddr, 0, sizeof dummyAddr); dummyAddr.ss_family = AF_INET;
                 Port dummyPort(0);
                 rtpGroupsock = new Groupsock(envir(), dummyAddr, dummyPort, 0);
                 rtcpGroupsock = new Groupsock(envir(), dummyAddr, dummyPort, 0);
                 LOG_WARN(">>> getStreamParameters: Created dummy RTP Groupsock (" << (int)rtpGroupsock << ") and dummy RTCP Groupsock (" << (int)rtcpGroupsock << ") for TCP mode.");
            }

            LOG_WARN(">>> getStreamParameters: Calling createNewRTPSource for session " << clientSessionId);
            rtpSource = createNewRTPSource(rtpGroupsock, 0, mediaSink);
            if (rtpSource == nullptr) {
                LOG_ERROR(">>> getStreamParameters: createNewRTPSource FAILED for session " << clientSessionId);
                if (mediaSink) Medium::close(mediaSink);
                delete rtpGroupsock;
                if (rtcpGroupsock != rtpGroupsock) delete rtcpGroupsock;
                return;
            }
             LOG_WARN(">>> getStreamParameters: createNewRTPSource SUCCEEDED for session " << clientSessionId << ". Source: " << (int)rtpSource);

        } else {
             LOG_ERROR(">>> getStreamParameters: Invalid parameters (no client ports or TCP socket) for session " << clientSessionId);
             if (mediaSink) Medium::close(mediaSink);
             return;
        }

        LOG_WARN(">>> getStreamParameters: Creating BackchannelStreamState for session " << clientSessionId);
        BackchannelStreamState* state = new BackchannelStreamState(*this, rtpSource, (BackchannelSink*)mediaSink, rtpGroupsock, rtcpGroupsock, clientSessionId);
        streamToken = fLastStreamToken = (void*)state;
        LOG_WARN(">>> getStreamParameters: Created BackchannelStreamState for session " << clientSessionId << ". State: " << (int)state << ". Assigned to streamToken.");
    }

    BackchannelDestinations* destinations;
    LOG_WARN(">>> getStreamParameters: Creating BackchannelDestinations for session " << clientSessionId);
    if (tcpSocketNum < 0) {
        destinations = new BackchannelDestinations(destinationAddress, clientRTPPort, clientRTCPPort);
    } else {
        destinations = new BackchannelDestinations(tcpSocketNum, rtpChannelId, rtcpChannelId, tlsState);
    }
    LOG_WARN(">>> getStreamParameters: Created BackchannelDestinations for session " << clientSessionId << ". Destinations: " << (int)destinations);

    LOG_WARN(">>> getStreamParameters: Adding Destinations to hash table for session " << clientSessionId);
    fDestinationsHashTable->Add((char const*)clientSessionId, destinations);
    LOG_WARN(">>> getStreamParameters: Added Destinations to hash table for session " << clientSessionId);

    LOG_WARN(">>> getStreamParameters FINISHED for session " << clientSessionId << ". streamToken OUT: " << (int)streamToken << ", destinations ADDED: " << (int)destinations);
}

void BackchannelServerMediaSubsession::startStream(unsigned clientSessionId, void* streamToken,
                             TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                             unsigned short& rtpSeqNum, unsigned& rtpTimestamp,
                             ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                             void* serverRequestAlternativeByteHandlerClientData)
 {
    LOG_WARN(">>> startStream CALLED for session " << clientSessionId << ". streamToken IN: " << (int)streamToken);
    BackchannelStreamState* state = (BackchannelStreamState*)streamToken;
    BackchannelDestinations* dests = (BackchannelDestinations*)(fDestinationsHashTable->Lookup((char const*)clientSessionId));
    LOG_WARN(">>> startStream looked up destinations for session " << clientSessionId << ". destinations FOUND: " << (int)dests);

    if (state == nullptr) {
        LOG_ERROR("startStream called with NULL streamToken for client session " << clientSessionId);
        return;
    }
     if (dests == nullptr) {
         LOG_ERROR("startStream failed to find Destinations for client session " << clientSessionId);
         return;
     }

    state->startPlaying(dests, rtcpRRHandler, rtcpRRHandlerClientData,
                        serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);

    RTPSource* rtpSource = state->rtpSource;
    if (rtpSource != nullptr) {
        rtpSeqNum = 0;
        rtpTimestamp = 0;
        LOG_DEBUG("startStream: Initializing rtpSeqNum=" << rtpSeqNum << ", rtpTimestamp=" << rtpTimestamp << " for session " << clientSessionId);
    } else {
         rtpSeqNum = 0;
         rtpTimestamp = 0;
         LOG_WARN("startStream: RTPSource is NULL in state for session " << clientSessionId << ". Setting SeqNum/Timestamp to 0.");
    }
 }


void BackchannelServerMediaSubsession::deleteStreamState(void*& streamToken) {
    char timeBuf[128];
    struct timeval now;
    gettimeofday(&now, NULL);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", localtime(&now.tv_sec));
    LOG_WARN(">>> deleteStreamState called at " << timeBuf << "." << (int)(now.tv_usec / 1000) << " with token: " << (int)streamToken);

    if (streamToken == nullptr) {
        LOG_WARN(">>> deleteStreamState: streamToken is already NULL. Returning.");
        return;
    }

    BackchannelStreamState* state = (BackchannelStreamState*)streamToken;
    LOG_DEBUG("deleteStreamState: State pointer from token: " << (int)state);
    if (state != nullptr) {
        unsigned currentRefCount = state->referenceCount();
        LOG_WARN(">>> deleteStreamState: Current reference count for session " << state->clientSessionId << " is " << currentRefCount << ". Decrementing...");
        if (state->decrementReferenceCount() == 0) {
            LOG_WARN(">>> deleteStreamState: Reference count is now zero. Proceeding with deletion for session " << state->clientSessionId);
            unsigned sid = state->clientSessionId;

            LOG_DEBUG("deleteStreamState: Looking up destinations for clientSessionId: " << sid);
            BackchannelDestinations* dests = (BackchannelDestinations*)(fDestinationsHashTable->Lookup((char const*)sid));
            LOG_DEBUG("deleteStreamState: Found destinations pointer: " << (int)dests << ". Removing from table if found.");
            if (dests) {
                LOG_WARN(">>> deleteStreamState: Removing Destinations for session " << sid << " from hash table.");
                fDestinationsHashTable->Remove((char const*)sid);
                LOG_WARN(">>> deleteStreamState: Deleting Destinations object for session " << sid);
                delete dests;
            } else {
                LOG_WARN(">>> deleteStreamState: No Destinations found in hash table for session " << sid << " to remove.");
            }

            if (streamToken == fLastStreamToken) {
                LOG_WARN(">>> deleteStreamState: Clearing fLastStreamToken because it matches the token being deleted.");
                fLastStreamToken = nullptr;
            }

            LOG_WARN(">>> deleteStreamState: Scheduling delayed deletion of BackchannelStreamState object for session " << sid << " at address " << (int)state);
            envir().taskScheduler().scheduleDelayedTask(0, (TaskFunc*)delayedDeleteTask, state);
            state = nullptr;

        } else {
             LOG_WARN(">>> deleteStreamState: Decremented reference count for client session " << state->clientSessionId << " to " << state->referenceCount() << ". State not deleted (token: " << (int)streamToken << ")");
        }
    } else {
        LOG_WARN(">>> deleteStreamState called with state pointer already NULL (token: " << (int)streamToken << ")");
    }

    if (state == nullptr) {
         LOG_WARN(">>> deleteStreamState: Clearing caller's streamToken pointer.");
         streamToken = nullptr;
    } else {
         LOG_WARN(">>> deleteStreamState: Not clearing caller's streamToken pointer (state=" << (int)state << ", refCount=" << state->referenceCount() << ")");
    }
}

void BackchannelServerMediaSubsession::closeStreamSource(FramedSource *inputSource) {
     Medium::close(inputSource);
}
void BackchannelServerMediaSubsession::closeStreamSink(MediaSink *outputSink) {
     Medium::close(outputSink);
}

void BackchannelServerMediaSubsession::getRTPSinkandRTCP(void* streamToken, RTPSink*& rtpSink, RTCPInstance*& rtcp) {
    rtpSink = nullptr;
    BackchannelStreamState* state = (BackchannelStreamState*)streamToken;
    if (state) {
        rtcp = state->rtcpInstance;
    } else {
        rtcp = nullptr;
    }
}
