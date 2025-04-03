#include "CustomRTSPClientSession.hpp"
#include "RTSPCommon.hh" // For handleCommand() etc.
#include "RTPInterface.hh" // For RTPSink, RTPSource
#include "ServerMediaSubsession.hh"
#include "BasicUsageEnvironment.hh"

#define MODULE "CustomRTSPClientSession" // For Logger

// Define a special track ID or name for the backchannel subsession
// This needs to match how the backchannel subsession is added in RTSP.cpp
const char* BACKCHANNEL_TRACK_ID = "track_backchannel"; // Example track ID

CustomRTSPClientSession::CustomRTSPClientSession(RTSPServer& ourServer, unsigned sessionId,
                                                 struct sockaddr_storage const& clientAddr,
                                                 Authenticator* authenticator)
    : RTSPServer::RTSPClientSession(ourServer, sessionId, clientAddr, authenticator),
      fBackchannelSink(nullptr),
      fBackchannelSubsession(nullptr) {
    LOG_DEBUG("CustomRTSPClientSession created (session ID " << sessionId << ")");
}

CustomRTSPClientSession::~CustomRTSPClientSession() {
    LOG_DEBUG("CustomRTSPClientSession destroyed (session ID " << fSessionId << ")");
    // Ensure sink is cleaned up if TEARDOWN wasn't called for it
    if (fBackchannelSink != nullptr) {
        LOG_WARN("Backchannel sink still active in destructor, cleaning up.");
        Medium::close(fBackchannelSink); // Use Medium::close for live555 objects
        fBackchannelSink = nullptr;
    }
}

void CustomRTSPClientSession::handleCmd_SETUP(RTSPClientConnection* ourClientConnection,
                                            char const* urlPreSuffix, char const* urlSuffix,
                                            char const* fullRequestStr) {
    // Call the base class implementation first to handle standard SETUP procedures
    // (parsing Transport header, finding subsession, setting up transport)
    RTSPServer::RTSPClientSession::handleCmd_SETUP(ourClientConnection, urlPreSuffix, urlSuffix, fullRequestStr);

    // Check if the base class handler failed (e.g., subsession not found, transport error)
    // The response code is stored in ourClientConnection->fResponseCode
    if (ourClientConnection->fResponseCode != 200) {
        LOG_WARN("Base handleCmd_SETUP failed with code " << ourClientConnection->fResponseCode << ". Aborting backchannel setup.");
        return;
    }

    // The base class handler should have found the ServerMediaSubsession.
    // We need to retrieve it. It's often stored temporarily during SETUP handling.
    // Looking at live555 source, it seems the subsession isn't directly stored
    // in RTSPClientSession after handleCmd_SETUP. We might need to re-find it
    // based on the urlSuffix or track ID used in the request.
    // Let's assume urlSuffix corresponds to the track ID.

    ServerMediaSession* sms = fOurServer.lookupServerMediaSession(urlPreSuffix);
    if (sms == NULL) {
        // This shouldn't happen if base SETUP succeeded, but check anyway
        LOG_ERROR("ServerMediaSession not found for preSuffix: " << urlPreSuffix);
        return;
    }

    ServerMediaSubsession* subsession = sms->lookupSubsession(urlSuffix);
    if (subsession == NULL) {
        LOG_ERROR("ServerMediaSubsession not found for suffix: " << urlSuffix);
        return;
    }

    LOG_DEBUG("Processing SETUP for track: " << subsession->trackId());

    // Check if this is our designated backchannel track
    // IMPORTANT: This requires the backchannel subsession to have a specific trackId()
    if (strcmp(subsession->trackId(), BACKCHANNEL_TRACK_ID) == 0) {
        LOG_INFO("Detected SETUP request for backchannel audio track.");

        // Verify the codec and parameters
        if (strcmp(subsession->codecName(), "PCMA") == 0 && subsession->rtpTimestampFrequency() == 8000) {
            LOG_INFO("Backchannel format verified (PCMA/8000).");

            // Prevent setting up the same backchannel sink multiple times
            if (fBackchannelSink != nullptr) {
                LOG_WARN("Backchannel sink already exists for this session. Ignoring new SETUP.");
                // Optionally send an error response? Or just let the base class response stand.
                return;
            }

            // Create and start our custom sink
            LOG_INFO("Creating BackchannelSink for target " << BACKCHANNEL_TARGET_IP << ":" << BACKCHANNEL_TARGET_PORT);
            fBackchannelSink = BackchannelSink::createNew(envir(),
                                                          BACKCHANNEL_TARGET_IP,
                                                          BACKCHANNEL_TARGET_PORT,
                                                          BACKCHANNEL_PAYLOAD_TYPE); // PCMA payload type = 8

            if (fBackchannelSink == nullptr) {
                LOG_ERROR("Failed to create BackchannelSink!");
                // Need to signal an error back to the client?
                // The base class already sent 200 OK. This is tricky.
                // Maybe set response code here and let the framework handle it?
                // ourClientConnection->fResponseCode = 500; // Internal Server Error
                // ourClientConnection->fResponseStatusMsg = strDup("Failed to create backchannel sink");
                return;
            }

            // We need the RTPSource that the base class setup created for this track.
            // This source delivers the RTP packets received from the client.
            RTPSource* rtpSource = subsession->rtpSource();
            if (rtpSource == nullptr) {
                 // This might happen if the base class setup failed silently or if using TCP transport?
                 // live555 setup for receiving RTP over TCP is different.
                 // For now, assume UDP and rtpSource should exist.
                 LOG_ERROR("RTPSource not found for backchannel subsession after SETUP!");
                 Medium::close(fBackchannelSink);
                 fBackchannelSink = nullptr;
                 // Signal error?
                 return;
            }

            LOG_INFO("Starting BackchannelSink playing from RTPSource.");
            // Connect our sink to the source. The sink's continuePlaying will pull data.
            fBackchannelSink->startPlaying(*rtpSource, NULL, NULL); // No completion funcs needed here

            // Keep track of which subsession is the backchannel
            fBackchannelSubsession = subsession;
            LOG_INFO("Backchannel setup complete for session " << fSessionId);

        } else {
            LOG_ERROR("Backchannel track SETUP requested, but format is not PCMA/8000. Codec: "
                      << subsession->codecName() << ", Freq: " << subsession->rtpTimestampFrequency());
            // Send an error? The base class already sent 200 OK.
            // Maybe 453 Not Enough Bandwidth or 461 Unsupported Transport?
            // ourClientConnection->fResponseCode = 461;
            // ourClientConnection->fResponseStatusMsg = strDup("Unsupported backchannel format");
        }
    } else {
        // This SETUP was for a different track (e.g., outgoing video/audio), ignore it here.
        LOG_DEBUG("SETUP was not for the backchannel track.");
    }
}

void CustomRTSPClientSession::handleCmd_TEARDOWN(RTSPClientConnection* ourClientConnection,
                                               ServerMediaSubsession* subsession) {
    LOG_DEBUG("Handling TEARDOWN for session " << fSessionId << ", subsession trackId: "
              << (subsession ? subsession->trackId() : "N/A (Session teardown)"));

    // Check if the teardown is for the specific backchannel subsession
    if (subsession != nullptr && subsession == fBackchannelSubsession && fBackchannelSink != nullptr) {
        LOG_INFO("Tearing down backchannel sink for track " << subsession->trackId());
        Medium::close(fBackchannelSink); // This should call stopPlaying() and the destructor
        fBackchannelSink = nullptr;
        fBackchannelSubsession = nullptr;
    } else if (subsession == nullptr && fBackchannelSink != nullptr) {
        // If subsession is NULL, it's a session-level TEARDOWN. Clean up our sink.
        LOG_INFO("Session-level TEARDOWN, cleaning up backchannel sink.");
        Medium::close(fBackchannelSink);
        fBackchannelSink = nullptr;
        fBackchannelSubsession = nullptr; // Should already be null if subsession is null, but be safe
    }

    // Call the base class implementation to handle the rest of the teardown
    RTSPServer::RTSPClientSession::handleCmd_TEARDOWN(ourClientConnection, subsession);
}
