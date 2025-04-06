#include "BackchannelServerMediaSubsession.hpp"
#include "BackchannelSink.hpp"
#include "Logger.hpp"
#include "globals.hpp" // Needed for cfg
#include "Config.hpp"  // Needed for cfg
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
#include <string> // Needed for std::string

#define MODULE "BackchannelSubsession"

static void delayedDeleteTask(void* clientData) {
    BackchannelStreamState* state = (BackchannelStreamState*)clientData;
    LOG_DEBUG("Deleting BackchannelStreamState");
    delete state;
}

BackchannelServerMediaSubsession* BackchannelServerMediaSubsession::createNew(UsageEnvironment& env, IMPBackchannelFormat format, Boolean reuseFirstSource) {
    return new BackchannelServerMediaSubsession(env, format, reuseFirstSource);
}

BackchannelServerMediaSubsession::BackchannelServerMediaSubsession(UsageEnvironment& env, IMPBackchannelFormat format, Boolean reuseFirstSource)
    : ServerMediaSubsession(env), fSDPLines(nullptr), /* fStreamData removed */
      fLastStreamToken(nullptr), fReuseFirstSource(reuseFirstSource),
      fInitialPortNum(6970), fMultiplexRTCPWithRTP(False), fFormat(format)
{
    LOG_DEBUG("Subsession created for channel " << static_cast<int>(fFormat));
    fDestinationsHashTable = HashTable::create(ONE_WORD_HASH_KEYS);
    gethostname(fCNAME, MAX_CNAME_LEN);
    fCNAME[MAX_CNAME_LEN] = '\0';

    if (!fMultiplexRTCPWithRTP) {
        fInitialPortNum = (fInitialPortNum + 1) & ~1;
    }
}

BackchannelServerMediaSubsession::~BackchannelServerMediaSubsession() {
    LOG_DEBUG("Subsession destroyed");
    delete[] fSDPLines;

    while (true) {
        BackchannelDestinations* destinations = (BackchannelDestinations*)(fDestinationsHashTable->RemoveNext());
        if (destinations == nullptr) break;
        LOG_DEBUG("Deleting Destinations object");
        delete destinations;
    }
    delete fDestinationsHashTable;
}

char const* BackchannelServerMediaSubsession::sdpLines(int /*addressFamily*/) {
    if (fSDPLines == nullptr) {
        LOG_DEBUG("Generating SDP lines");

        // Increase size for potential fmtp line
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

        LOG_DEBUG("Generating SDP for format " << formatName << " (Payload Type: " << payloadType << ")");

        // Construct fmtp line only for Opus
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
        LOG_DEBUG("Backchannel SDP: " << fSDPLines);
    }
    return fSDPLines;
}

MediaSink* BackchannelServerMediaSubsession::createNewStreamDestination(unsigned clientSessionId, unsigned& estBitrate) {
    // Estimate bitrate based on format? Opus can be lower.
    if (fFormat == IMPBackchannelFormat::OPUS) {
         estBitrate = 32; // Example Opus bitrate
    } else {
         estBitrate = 64; // G.711 bitrate
    }
    LOG_INFO("Creating BackchannelSink for channel: " << static_cast<int>(fFormat) << " (est bitrate: " << estBitrate << ")");
    return BackchannelSink::createNew(envir(), clientSessionId, fFormat);
}

RTPSource* BackchannelServerMediaSubsession::createNewRTPSource(Groupsock* rtpGroupsock, unsigned char /*rtpPayloadTypeIfDynamic*/, MediaSink* /*outputSink*/) {
    LOG_DEBUG("Creating new RTPSource for channel " << static_cast<int>(fFormat));

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
                                      0, // Use determined channel count
                                      False); // allowMultipleFramesPerPacket
}

Boolean BackchannelServerMediaSubsession::getServerPorts(Port& rtpPort, Port& rtcpPort) {
    if (fLastStreamToken == nullptr) return False;
    BackchannelStreamState* lastState = (BackchannelStreamState*)fLastStreamToken;
    if (lastState->rtpGS == nullptr || lastState->rtcpGS == nullptr) {
         LOG_DEBUG("Cannot reuse state for UDP ports (likely TCP state).");
         return False;
    }
    rtpPort = lastState->rtpGS->port();
    rtcpPort = lastState->rtcpGS->port();
    return True;
}

char const* BackchannelServerMediaSubsession::getAuxSDPLine(RTPSink* /*rtpSink*/, FramedSource* /*inputSource*/) {
    // Could potentially add fmtp line here instead of sdpLines if needed
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
    LOG_DEBUG("getStreamParameters input for session " << clientSessionId
             << ": clientRTPPort=" << ntohs(clientRTPPort.num())
             << ", clientRTCPPort=" << ntohs(clientRTCPPort.num())
             << ", tcpSocketNum=" << tcpSocketNum);

    LOG_DEBUG("getStreamParameters for session " << clientSessionId);
    isMulticast = False;
    streamToken = nullptr;

    if (addressIsNull(destinationAddress)) {
        destinationAddress = clientAddress;
    }

    if (fLastStreamToken != nullptr && fReuseFirstSource) {
        LOG_INFO("Reusing stream state for session " << clientSessionId);
        BackchannelStreamState* lastState = (BackchannelStreamState*)fLastStreamToken;
        if (lastState->rtpGS == nullptr || lastState->rtcpGS == nullptr) {
             LOG_ERROR("Failed to get server ports from reusable stream state (not UDP?)!");
             return;
        }
        serverRTPPort = lastState->rtpGS->port();
        serverRTCPPort = lastState->rtcpGS->port();
        lastState->incrementReferenceCount();
        streamToken = fLastStreamToken;
        LOG_DEBUG("Reused state: Ref count now " << lastState->referenceCount());
    } else {
        LOG_DEBUG("Not reusing state for session " << clientSessionId << ". Creating new state.");
        unsigned streamBitrate = 0;
        MediaSink* mediaSink = nullptr;
        RTPSource* rtpSource = nullptr;
        Groupsock* rtpGroupsock = nullptr;
        Groupsock* rtcpGroupsock = nullptr;

        LOG_DEBUG("Calling createNewStreamDestination for session " << clientSessionId);
        mediaSink = createNewStreamDestination(clientSessionId, streamBitrate);
        if (mediaSink == nullptr) {
            LOG_ERROR(">>> getStreamParameters: createNewStreamDestination FAILED for session " << clientSessionId);
            return;
        }
        LOG_DEBUG("createNewStreamDestination succeeded for session " << clientSessionId << ". Sink: " << (int)mediaSink);

        if (clientRTPPort.num() != 0 || tcpSocketNum >= 0) {
            if (clientRTCPPort.num() == 0 && tcpSocketNum < 0) {
                 LOG_ERROR("Raw UDP streaming not supported for backchannel");
                 if (mediaSink) Medium::close(mediaSink);
                 return;
            } else if (tcpSocketNum < 0) {
                LOG_DEBUG("Attempting UDP port allocation for session " << clientSessionId);
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
                     LOG_INFO("Attempting to increase RTP socket receive buffer for session " << clientSessionId << " to " << requestedSize);
                     increaseReceiveBufferTo(envir(), rtpGroupsock->socketNum(), requestedSize);
                 }
                 LOG_DEBUG("UDP port allocation succeeded for session " << clientSessionId << ". RTP=" << ntohs(serverRTPPort.num()) << ", RTCP=" << ntohs(serverRTCPPort.num()));
             } else {
                  LOG_DEBUG("Client requested TCP interleaved mode for session " << clientSessionId);
                 serverRTPPort = 0;
                 serverRTCPPort = 0;
                 rtpGroupsock = nullptr;
                 rtcpGroupsock = nullptr;
                 struct sockaddr_storage dummyAddr; memset(&dummyAddr, 0, sizeof dummyAddr); dummyAddr.ss_family = AF_INET;
                 Port dummyPort(0);
                 rtpGroupsock = new Groupsock(envir(), dummyAddr, dummyPort, 0);
                 rtcpGroupsock = new Groupsock(envir(), dummyAddr, dummyPort, 0);
                 LOG_DEBUG("Created dummy RTP Groupsock (" << (int)rtpGroupsock << ") and dummy RTCP Groupsock (" << (int)rtcpGroupsock << ") for TCP mode.");
            }

            LOG_DEBUG("Calling createNewRTPSource for session " << clientSessionId);
            rtpSource = createNewRTPSource(rtpGroupsock, 0, mediaSink);
            if (rtpSource == nullptr) {
                LOG_ERROR(">>> getStreamParameters: createNewRTPSource FAILED for session " << clientSessionId);
                if (mediaSink) Medium::close(mediaSink);
                delete rtpGroupsock;
                if (rtcpGroupsock != rtpGroupsock) delete rtcpGroupsock;
                return;
            }
             LOG_DEBUG("createNewRTPSource succeeded for session " << clientSessionId << ". Source: " << (int)rtpSource);

        } else {
             LOG_ERROR(">>> getStreamParameters: Invalid parameters (no client ports or TCP socket) for session " << clientSessionId);
             if (mediaSink) Medium::close(mediaSink);
             return;
        }

        LOG_DEBUG("Creating StreamState for session " << clientSessionId);
        BackchannelStreamState* state = new BackchannelStreamState(*this, rtpSource, (BackchannelSink*)mediaSink, rtpGroupsock, rtcpGroupsock, clientSessionId);
        streamToken = fLastStreamToken = (void*)state;
        LOG_DEBUG("Created StreamState for session " << clientSessionId << ". State: " << (int)state << ". Assigned to streamToken.");
    }

    BackchannelDestinations* destinations;
    LOG_DEBUG("Creating Destinations for session " << clientSessionId);
    if (tcpSocketNum < 0) {
        destinations = new BackchannelDestinations(destinationAddress, clientRTPPort, clientRTCPPort);
    } else {
        destinations = new BackchannelDestinations(tcpSocketNum, rtpChannelId, rtcpChannelId, tlsState);
    }
    LOG_DEBUG("Created Destinations for session " << clientSessionId << ". Destinations: " << (int)destinations);

    LOG_DEBUG("Adding Destinations to hash table for session " << clientSessionId);
    fDestinationsHashTable->Add((char const*)clientSessionId, destinations);
    LOG_DEBUG("Added Destinations to hash table for session " << clientSessionId);

    LOG_DEBUG("getStreamParameters complete for session " << clientSessionId << ". streamToken: " << (int)streamToken << ", destinations: " << (int)destinations);
}

void BackchannelServerMediaSubsession::startStream(unsigned clientSessionId, void* streamToken,
                             TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                             unsigned short& rtpSeqNum, unsigned& rtpTimestamp,
                             ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                             void* serverRequestAlternativeByteHandlerClientData)
 {
    LOG_DEBUG("startStream for session " << clientSessionId << ". streamToken: " << (int)streamToken);
    BackchannelStreamState* state = (BackchannelStreamState*)streamToken;
    BackchannelDestinations* dests = (BackchannelDestinations*)(fDestinationsHashTable->Lookup((char const*)clientSessionId));
    LOG_DEBUG("startStream looked up destinations for session " << clientSessionId << ". destinations: " << (int)dests);

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
         LOG_WARN("RTPSource is NULL in state for session " << clientSessionId << ". Setting SeqNum/Timestamp to 0.");
    }
 }


void BackchannelServerMediaSubsession::deleteStreamState(void*& streamToken) {
    char timeBuf[128];
    struct timeval now;
    gettimeofday(&now, NULL);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", localtime(&now.tv_sec));
    LOG_DEBUG("deleteStreamState called at " << timeBuf << "." << (int)(now.tv_usec / 1000) << " with token: " << (int)streamToken);

    if (streamToken == nullptr) {
        LOG_DEBUG("streamToken is already NULL. Returning.");
        return;
    }

    BackchannelStreamState* state = (BackchannelStreamState*)streamToken;
    LOG_DEBUG("deleteStreamState: State pointer from token: " << (int)state);
    if (state != nullptr) {
        unsigned currentRefCount = state->referenceCount();
        LOG_DEBUG("Current ref count for session " << state->clientSessionId << " is " << currentRefCount << ". Decrementing...");
        if (state->decrementReferenceCount() == 0) {
            LOG_DEBUG("Ref count is zero. Proceeding with deletion for session " << state->clientSessionId);
            unsigned sid = state->clientSessionId;

            LOG_DEBUG("deleteStreamState: Looking up destinations for clientSessionId: " << sid);
            BackchannelDestinations* dests = (BackchannelDestinations*)(fDestinationsHashTable->Lookup((char const*)sid));
            LOG_DEBUG("deleteStreamState: Found destinations pointer: " << (int)dests << ". Removing from table if found.");
            if (dests) {
                LOG_DEBUG("Removing Destinations for session " << sid << " from hash table.");
                fDestinationsHashTable->Remove((char const*)sid);
                LOG_DEBUG("Deleting Destinations object for session " << sid);
                delete dests;
            } else {
                LOG_DEBUG("No Destinations found in hash table for session " << sid << " to remove.");
            }

            if (streamToken == fLastStreamToken) {
                LOG_DEBUG("Clearing fLastStreamToken because it matches the token being deleted.");
                fLastStreamToken = nullptr;
            }

            LOG_DEBUG("Scheduling delayed deletion of StreamState object for session " << sid << " at address " << (int)state);
            envir().taskScheduler().scheduleDelayedTask(0, (TaskFunc*)delayedDeleteTask, state);
            state = nullptr;

        } else {
             LOG_DEBUG("Decremented ref count for session " << state->clientSessionId << " to " << state->referenceCount() << ". State not deleted (token: " << (int)streamToken << ")");
        }
    } else {
        LOG_DEBUG("deleteStreamState called with state pointer already NULL (token: " << (int)streamToken << ")");
    }

    if (state == nullptr) {
         LOG_DEBUG("Clearing caller's streamToken pointer.");
         streamToken = nullptr;
    } else {
         LOG_DEBUG("Not clearing caller's streamToken pointer (state=" << (int)state << ", refCount=" << state->referenceCount() << ")");
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
