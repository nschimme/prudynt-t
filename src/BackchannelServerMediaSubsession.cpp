// Project Headers
#include "BackchannelServerMediaSubsession.hpp"
#include "BackchannelSink.hpp"
#include "Logger.hpp"
#include "globals.hpp" // For backchannel_stream definition

// Live555 Headers
#include <NetAddress.hh> // Moved include earlier
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <SimpleRTPSource.hh> // Use SimpleRTPSource for receiving
#include <HashTable.hh>

// Standard C/C++ Headers
#include <cassert>
#include <unistd.h> // For gethostname
#include <cstring>  // For memset
#include <sys/time.h> // Needed for gettimeofday

#define MODULE "BackchannelSubsession"

// Define payload types (consistent with sdpLines)
#define PCMU_PAYLOAD_TYPE 0
#define PCMA_PAYLOAD_TYPE 8
#define BACKCHANNEL_TIMESTAMP_FREQ 8000 // 8 kHz for PCMU/PCMA

// ---------------------------------------------------------------------------
// BackchannelServerMediaSubsession Implementation
// ---------------------------------------------------------------------------

// Updated createNew signature
BackchannelServerMediaSubsession* BackchannelServerMediaSubsession::createNew(UsageEnvironment& env, backchannel_stream* stream_data, Boolean reuseFirstSource) {
    return new BackchannelServerMediaSubsession(env, stream_data, reuseFirstSource);
}

// Updated constructor signature and initialization list
BackchannelServerMediaSubsession::BackchannelServerMediaSubsession(UsageEnvironment& env, backchannel_stream* stream_data, Boolean reuseFirstSource)
    : ServerMediaSubsession(env), fSDPLines(nullptr), fStreamData(stream_data),
      fLastStreamToken(nullptr), fReuseFirstSource(reuseFirstSource), // Store reuse flag, init last token
      fInitialPortNum(6970), fMultiplexRTCPWithRTP(False) // Initialize new members
{
    LOG_DEBUG("BackchannelServerMediaSubsession created (using ServerMediaSubsession base)");
    fDestinationsHashTable = HashTable::create(ONE_WORD_HASH_KEYS); // Initialize destinations table
    // Ensure fStreamData is valid if needed later
    if (fStreamData == nullptr) {
        LOG_WARN("backchannel_stream data provided is null!");
        // Consider if this is a fatal error
    }
    // Initialize CNAME
    gethostname(fCNAME, MAX_CNAME_LEN);
    fCNAME[MAX_CNAME_LEN] = '\0'; // Ensure null termination

    // Ensure ports are even if not multiplexing (from reference constructor)
    if (!fMultiplexRTCPWithRTP) {
        fInitialPortNum = (fInitialPortNum + 1) & ~1;
    }
}

BackchannelServerMediaSubsession::~BackchannelServerMediaSubsession() {
    LOG_DEBUG("BackchannelServerMediaSubsession destroyed");
    delete[] fSDPLines;

    // Clean out the destinations hash table:
    while (true) {
        BackchannelDestinations* destinations = (BackchannelDestinations*)(fDestinationsHashTable->RemoveNext()); // Use renamed struct
        if (destinations == nullptr) break;
        LOG_DEBUG("Deleting BackchannelDestinations object"); // Use renamed struct
        delete destinations;
    }
    delete fDestinationsHashTable;
    // Note: Stream states pointed to by fLastStreamToken (if any)
    // should be cleaned up via deleteStreamState calls from RTSPServer.
}

// Generate the SDP description for this subsession
// Keeping the original simple SDP generation for recvonly
char const* BackchannelServerMediaSubsession::sdpLines(int /*addressFamily*/) {
    if (fSDPLines == nullptr) {
        LOG_DEBUG("Generating SDP lines for backchannel (addressFamily ignored)");

        unsigned int sdpLinesSize = 250;
        fSDPLines = new char[sdpLinesSize];
        if (fSDPLines == nullptr) return nullptr;

        snprintf(fSDPLines, sdpLinesSize,
                 "m=audio 0 RTP/AVP %d %d\r\n"
                 "c=IN IP4 0.0.0.0\r\n"
                 "b=AS:64\r\n"
                 "a=rtpmap:%d PCMU/%d/1\r\n"
                 "a=rtpmap:%d PCMA/%d/1\r\n"
                 "a=control:%s\r\n"
                 "a=recvonly\r\n",
                 PCMU_PAYLOAD_TYPE, PCMA_PAYLOAD_TYPE,
                 PCMU_PAYLOAD_TYPE, BACKCHANNEL_TIMESTAMP_FREQ,
                 PCMA_PAYLOAD_TYPE, BACKCHANNEL_TIMESTAMP_FREQ,
                 trackId()
        );

        fSDPLines[sdpLinesSize - 1] = '\0'; // Null-terminate
    }
    return fSDPLines;
}

// Creates the MediaSink (our BackchannelSink) - Kept from original
MediaSink* BackchannelServerMediaSubsession::createNewStreamDestination(unsigned clientSessionId, unsigned& estBitrate) {
    estBitrate = 64; // kbps
    LOG_DEBUG("Creating new BackchannelSink for client session " << clientSessionId);
    return BackchannelSink::createNew(envir(), fStreamData);
}

// Creates the RTPSource (e.g., SimpleRTPSource) - Kept from original
RTPSource* BackchannelServerMediaSubsession::createNewRTPSource(Groupsock* rtpGroupsock, unsigned char /*rtpPayloadTypeIfDynamic*/, MediaSink* /*outputSink*/) {
    LOG_DEBUG("Creating new SimpleRTPSource for backchannel");
    // Payload type doesn't matter much for SimpleRTPSource when receiving static types
    return SimpleRTPSource::createNew(envir(), rtpGroupsock, 0 /* Placeholder PT */,
                                      BACKCHANNEL_TIMESTAMP_FREQ, nullptr /* mime type */,
                                      0 /* num channels */,
                                      False /* no RTCP - handled by StreamState */);
}

// Helper to get server ports from existing state (for reuse logic) - Kept from original
Boolean BackchannelServerMediaSubsession::getServerPorts(Port& rtpPort, Port& rtcpPort) {
    if (fLastStreamToken == nullptr) return False;
    // Need to cast to the correct StreamState type used by the reference logic
    // Assuming BackchannelStreamState is compatible or we adapt it.
    BackchannelStreamState* lastState = (BackchannelStreamState*)fLastStreamToken;
    if (lastState->rtpGS == nullptr || lastState->rtcpGS == nullptr) {
         LOG_WARN("getServerPorts: Cannot reuse state as it lacks UDP groupsocks (likely TCP state).");
         return False;
    }
    rtpPort = lastState->rtpGS->port();
    rtcpPort = lastState->rtcpGS->port();
    return True;
}

// Helper for SDP generation (not needed for recvonly) - Kept from original
char const* BackchannelServerMediaSubsession::getAuxSDPLine(RTPSink* /*rtpSink*/, FramedSource* /*inputSource*/) {
    return nullptr;
}

// ---------------------------------------------------------------------------
// getStreamParameters - Replaced with logic from OnDemandServerMediaSubsession_BC
// ---------------------------------------------------------------------------
void BackchannelServerMediaSubsession::getStreamParameters(unsigned clientSessionId,
                                     struct sockaddr_storage const& clientAddress,
                                     Port const& clientRTPPort,
                                     Port const& clientRTCPPort,
                                     int tcpSocketNum,
                                     unsigned char rtpChannelId,
                                     unsigned char rtcpChannelId,
                                     TLSState* tlsState,
                                     struct sockaddr_storage& destinationAddress,
                                     u_int8_t& /*destinationTTL*/, // Not used directly by reference
                                     Boolean& isMulticast,
                                     Port& serverRTPPort,
                                     Port& serverRTCPPort,
                                     void*& streamToken)
{
    LOG_WARN(">>> getStreamParameters ENTERED for session " << clientSessionId);
    isMulticast = False; // Always unicast for this type
    streamToken = nullptr;

    // Handle destination address (from reference)
    if (addressIsNull(destinationAddress)) {
        // normal case - use the client address as the destination address:
        destinationAddress = clientAddress;
    }

    // Check for reuse FIRST (from reference)
    if (fLastStreamToken != nullptr && fReuseFirstSource) {
        LOG_INFO("Reusing existing stream state for client session " << clientSessionId);
        // Retrieve server ports from the existing state
        BackchannelStreamState* lastState = (BackchannelStreamState*)fLastStreamToken;
        if (lastState->rtpGS == nullptr || lastState->rtcpGS == nullptr) {
             LOG_ERROR("Failed to get server ports from reusable stream state (not UDP?)!");
             return; // Cannot reuse non-UDP state? Or handle differently?
        }
        serverRTPPort = lastState->rtpGS->port();
        serverRTCPPort = lastState->rtcpGS->port();
        lastState->incrementReferenceCount();
        streamToken = fLastStreamToken;
        LOG_DEBUG("getStreamParameters (reused state): Ref count now " << lastState->referenceCount());
    } else {
        // Normal case: Create a new media source and stream state:
        LOG_WARN(">>> getStreamParameters: Not reusing state for session " << clientSessionId << ". Proceeding to create new state.");
        unsigned streamBitrate = 0; // Not strictly needed for sink/source creation here
        MediaSink* mediaSink = nullptr;
        RTPSource* rtpSource = nullptr;
        Groupsock* rtpGroupsock = nullptr;
        Groupsock* rtcpGroupsock = nullptr;
        /* BasicUDPSource* udpSource = nullptr; // Removed unused variable */

        // Create the sink (our specific implementation)
        LOG_WARN(">>> getStreamParameters: Calling createNewStreamDestination for session " << clientSessionId);
        mediaSink = createNewStreamDestination(clientSessionId, streamBitrate); // Use our override
        if (mediaSink == nullptr) {
            LOG_ERROR(">>> getStreamParameters: createNewStreamDestination FAILED for session " << clientSessionId);
            return;
        }
        LOG_WARN(">>> getStreamParameters: createNewStreamDestination SUCCEEDED for session " << clientSessionId << ". Sink: " << (int)mediaSink);

        // Create groupsocks and allocate ports if UDP
        if (clientRTPPort.num() != 0 || tcpSocketNum >= 0) { // From reference (check if client ports or TCP socket provided)
            if (clientRTCPPort.num() == 0 && tcpSocketNum < 0) {
                 LOG_ERROR("Raw UDP streaming not supported for backchannel");
                 // Cleanup sink
                 if (mediaSink) Medium::close(mediaSink);
                 return;
            } else if (tcpSocketNum < 0) { // Normal UDP case
                LOG_WARN(">>> getStreamParameters: Attempting UDP port allocation for session " << clientSessionId);
                NoReuse dummy(envir()); // ensures that we skip over ports that are already in use
                portNumBits serverPortNum = fInitialPortNum;
                while(1) {
                    serverRTPPort = serverPortNum;
                    // Use null address for listening
                    struct sockaddr_storage nullAddr;
                    memset(&nullAddr, 0, sizeof(nullAddr));
                    nullAddr.ss_family = AF_INET; // Assuming IPv4 for simplicity
                    rtpGroupsock = new Groupsock(envir(), nullAddr, serverRTPPort, 255);
                    if (rtpGroupsock->socketNum() < 0) {
                        delete rtpGroupsock; rtpGroupsock = nullptr;
                        serverPortNum += (fMultiplexRTCPWithRTP ? 1 : 2); // Increment correctly
                        continue; // try again
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
                            ++serverPortNum; // Skip this pair
                            continue; // try again
                        }
                    }
                    break; // success
                }
                 LOG_WARN(">>> getStreamParameters: UDP port allocation SUCCEEDED for session " << clientSessionId << ". RTP=" << ntohs(serverRTPPort.num()) << ", RTCP=" << ntohs(serverRTCPPort.num()));
            } else { // TCP Interleaved
                 LOG_WARN(">>> getStreamParameters: Client requested TCP interleaved mode for session " << clientSessionId);
                 serverRTPPort = 0;
                 serverRTCPPort = 0;
                 rtpGroupsock = nullptr;
                 rtcpGroupsock = nullptr;
                 // --- Create a dummy groupsock for TCP mode ---
                 // SimpleRTPSource might still require a valid Groupsock object even if not used for UDP.
                 struct sockaddr_storage dummyAddr; memset(&dummyAddr, 0, sizeof dummyAddr); dummyAddr.ss_family = AF_INET; // Use AF_INET or AF_INET6
                 Port dummyPort(0);
                 rtpGroupsock = new Groupsock(envir(), dummyAddr, dummyPort, 0); // Create dummy
                 LOG_WARN(">>> getStreamParameters: Created dummy Groupsock (" << (int)rtpGroupsock << ") for TCP mode.");
                 // We'll need to ensure this dummy groupsock is deleted later in BackchannelStreamState destructor if created.
                 // For now, rtcpGroupsock remains null for TCP.
            }

            // Create the RTPSource (our specific implementation)
            LOG_WARN(">>> getStreamParameters: Calling createNewRTPSource for session " << clientSessionId);
            // Payload type doesn't matter much here for SimpleRTPSource
            rtpSource = createNewRTPSource(rtpGroupsock, 0, mediaSink); // Use our override
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

        // Create and store the stream state (using our state class)
        LOG_WARN(">>> getStreamParameters: Creating BackchannelStreamState for session " << clientSessionId);
        // Pass necessary components to state constructor
        // Note: For TCP, rtcpGroupsock is NULL, rtpGroupsock is the dummy one.
        BackchannelStreamState* state = new BackchannelStreamState(*this, rtpSource, (BackchannelSink*)mediaSink, rtpGroupsock, rtcpGroupsock, clientSessionId);
        streamToken = fLastStreamToken = (void*)state;
        LOG_WARN(">>> getStreamParameters: Created BackchannelStreamState for session " << clientSessionId << ". State: " << (int)state << ". Assigned to streamToken.");
    }

    // Record these destinations as being for this client session id (common logic)
    BackchannelDestinations* destinations;
    LOG_WARN(">>> getStreamParameters: Creating BackchannelDestinations for session " << clientSessionId);
    if (tcpSocketNum < 0) { // UDP
        destinations = new BackchannelDestinations(destinationAddress, clientRTPPort, clientRTCPPort); // Use destinationAddress now
    } else { // TCP
        destinations = new BackchannelDestinations(tcpSocketNum, rtpChannelId, rtcpChannelId, tlsState);
    }
    LOG_WARN(">>> getStreamParameters: Created BackchannelDestinations for session " << clientSessionId << ". Destinations: " << (int)destinations);

    LOG_WARN(">>> getStreamParameters: Adding Destinations to hash table for session " << clientSessionId);
    fDestinationsHashTable->Add((char const*)clientSessionId, destinations);
    LOG_WARN(">>> getStreamParameters: Added Destinations to hash table for session " << clientSessionId);

    LOG_WARN(">>> getStreamParameters FINISHED for session " << clientSessionId << ". streamToken OUT: " << (int)streamToken << ", destinations ADDED: " << (int)destinations);
}

// ---------------------------------------------------------------------------
// startStream - Replaced with logic from OnDemandServerMediaSubsession_BC
// ---------------------------------------------------------------------------
void BackchannelServerMediaSubsession::startStream(unsigned clientSessionId, void* streamToken,
                             TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                             unsigned short& rtpSeqNum, unsigned& rtpTimestamp, // Use these now
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
         // Maybe delete state here as it's orphaned? Consider implications.
         return;
     }

    // Call the state's startPlaying method (which now handles RTCP setup etc.)
    state->startPlaying(dests, rtcpRRHandler, rtcpRRHandlerClientData,
                        serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);

    // Set output sequence number and timestamp (from reference)
    // These might be less relevant for a recvonly sink, but follow the pattern.
    RTPSource* rtpSource = state->rtpSource; // Get source from state
    if (rtpSource != nullptr) {
        // Note: For a receiver, curPacketRTPSeqNum/Timestamp might not be meaningful
        // until packets arrive. Initialize to 0 or a sensible default.
        rtpSeqNum = 0; // rtpSource->curPacketRTPSeqNum();
        rtpTimestamp = 0; // rtpSource->curPacketRTPTimestamp();
        LOG_DEBUG("startStream: Initializing rtpSeqNum=" << rtpSeqNum << ", rtpTimestamp=" << rtpTimestamp << " for session " << clientSessionId);
    } else {
         rtpSeqNum = 0;
         rtpTimestamp = 0;
         LOG_WARN("startStream: RTPSource is NULL in state for session " << clientSessionId << ". Setting SeqNum/Timestamp to 0.");
    }
 }


// ---------------------------------------------------------------------------
// deleteStreamState - Kept mostly original, ensure compatibility with new state mgmt
// ---------------------------------------------------------------------------
void BackchannelServerMediaSubsession::deleteStreamState(void*& streamToken) {
    // --- Enhanced Logging ---
    char timeBuf[128];
    struct timeval now;
    gettimeofday(&now, NULL);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", localtime(&now.tv_sec));
    LOG_WARN(">>> deleteStreamState called at " << timeBuf << "." << (int)(now.tv_usec / 1000) << " with token: " << (int)streamToken);
    // --- End Enhanced Logging ---

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

            // Remove and delete destinations from hash table
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

            // Clear fLastStreamToken if it matches the one being deleted
            if (streamToken == fLastStreamToken) {
                LOG_WARN(">>> deleteStreamState: Clearing fLastStreamToken because it matches the token being deleted.");
                fLastStreamToken = nullptr;
            }

            // Delete the state object itself (destructor handles internal cleanup)
            LOG_WARN(">>> deleteStreamState: Deleting BackchannelStreamState object for session " << sid);
            delete state;
            state = nullptr; // Avoid dangling pointer check below

        } else {
             LOG_WARN(">>> deleteStreamState: Decremented reference count for client session " << state->clientSessionId << " to " << state->referenceCount() << ". State not deleted.");
        }
    } else {
        LOG_WARN(">>> deleteStreamState called with state pointer already NULL (token: " << (int)streamToken << ")");
    }

    // Clear the caller's streamToken pointer *only* if the state was actually deleted
    if (state == nullptr) { // Check if delete happened above
         LOG_WARN(">>> deleteStreamState: Clearing caller's streamToken pointer.");
         streamToken = nullptr;
    } else {
         LOG_WARN(">>> deleteStreamState: Not clearing caller's streamToken pointer (state=" << (int)state << ", refCount=" << state->referenceCount() << ")");
    }
}

// --- Methods inherited from ServerMediaSubsession but not strictly needed for basic recvonly ---

// Kept original implementations
void BackchannelServerMediaSubsession::closeStreamSource(FramedSource *inputSource) {
     Medium::close(inputSource);
}
void BackchannelServerMediaSubsession::closeStreamSink(MediaSink *outputSink) {
     Medium::close(outputSink);
}

// Kept original implementation
void BackchannelServerMediaSubsession::getRTPSinkandRTCP(void* streamToken, RTPSink*& rtpSink, RTCPInstance*& rtcp) {
    rtpSink = nullptr; // We don't send RTP
    // Retrieve RTCP from StreamState if needed (as done in BackchannelStreamState::startPlaying)
    BackchannelStreamState* state = (BackchannelStreamState*)streamToken;
    if (state) {
        rtcp = state->rtcpInstance; // Access the instance created in startPlaying
    } else {
        rtcp = nullptr;
    }
}
