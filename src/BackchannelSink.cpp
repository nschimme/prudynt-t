#include "BackchannelSink.hpp"
#include "GroupsockHelper.hh" // For socket operations and non-blocking setup
#include "RTPInterface.hh"    // For RTP header parsing (optional, but good practice)

#define MODULE "BackchannelSink" // For Logger

BackchannelSink* BackchannelSink::createNew(UsageEnvironment& env,
                                            const char* targetIp,
                                            portNumBits targetPort,
                                            unsigned char rtpPayloadFormat) {
    return new BackchannelSink(env, targetIp, targetPort, rtpPayloadFormat);
}

BackchannelSink::BackchannelSink(UsageEnvironment& env,
                                 const char* targetIp,
                                 portNumBits targetPort,
                                 unsigned char rtpPayloadFormat)
    : MediaSink(env),
      fRtpPayloadFormat(rtpPayloadFormat),
      fTargetIp(strdup(targetIp)), // Remember to free in destructor
      fTargetPort(targetPort),
      fSocket(-1),
      fIsConnected(false) {
    fReceiveBuffer = new u_int8_t[kReceiveBufferSize];
    if (fReceiveBuffer == NULL) {
        LOG_ERROR("Failed to allocate receive buffer");
        // Handle allocation failure if necessary
    }

    memset(&fTargetAddr, 0, sizeof(fTargetAddr));
    fTargetAddr.sin_family = AF_INET;
    fTargetAddr.sin_port = htons(fTargetPort);
    if (inet_pton(AF_INET, fTargetIp, &fTargetAddr.sin_addr) <= 0) {
        LOG_ERROR("Invalid target IP address: " << fTargetIp);
        // Handle invalid IP if necessary
    }
}

BackchannelSink::~BackchannelSink() {
    delete[] fReceiveBuffer;
    free(fTargetIp);
    closeConnection();
}

void BackchannelSink::stopPlaying() {
    MediaSink::stopPlaying(); // Call base class implementation
    closeConnection();
}

bool BackchannelSink::connectToTarget() {
    if (fIsConnected) return true;

    fSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (fSocket < 0) {
        LOG_ERROR("Failed to create TCP socket: " << strerror(errno));
        return false;
    }

    // Optional: Set non-blocking for connect attempt
    // makeSocketNonBlocking(fSocket);

    LOG_INFO("Connecting to " << fTargetIp << ":" << fTargetPort);
    if (connect(fSocket, (struct sockaddr*)&fTargetAddr, sizeof(fTargetAddr)) < 0) {
        // For non-blocking connect, check errno for EINPROGRESS and handle later
        if (errno != EINPROGRESS) {
            LOG_ERROR("Failed to connect to target: " << strerror(errno));
            close(fSocket);
            fSocket = -1;
            return false;
        }
        // Handle non-blocking connect completion if needed (e.g., using select/poll)
        // For simplicity here, we assume blocking connect or handle completion elsewhere
        LOG_WARN("Connect attempt in progress (assuming blocking for now)");
        // If truly non-blocking, connection success needs confirmation later.
        // For this example, let's assume blocking connect succeeded or failed immediately.
        // A robust implementation would handle EINPROGRESS properly.
    }

    // If connect succeeded immediately (or assuming blocking)
    LOG_INFO("Successfully connected to " << fTargetIp << ":" << fTargetPort);
    fIsConnected = true;

    // Make the socket non-blocking for subsequent sends/recvs if desired
    // makeSocketNonBlocking(fSocket);

    return true;
}

void BackchannelSink::closeConnection() {
    if (fSocket >= 0) {
        LOG_INFO("Closing connection to " << fTargetIp << ":" << fTargetPort);
        close(fSocket);
        fSocket = -1;
    }
    fIsConnected = false;
}

bool BackchannelSink::sendData(u_int8_t* data, unsigned size) {
    if (!fIsConnected) {
        LOG_WARN("Attempted to send data while not connected. Trying to reconnect...");
        if (!connectToTarget()) {
            LOG_ERROR("Reconnect failed. Cannot send data.");
            return false;
        }
    }

    ssize_t bytesSent = send(fSocket, data, size, 0); // Consider MSG_NOSIGNAL if needed
    if (bytesSent < 0) {
        LOG_ERROR("Failed to send data: " << strerror(errno));
        // Handle specific errors like EPIPE (connection closed)
        if (errno == EPIPE || errno == ECONNRESET) {
            closeConnection();
        }
        return false;
    } else if ((unsigned)bytesSent < size) {
        LOG_WARN("Partial send: sent " << bytesSent << " of " << size << " bytes");
        // Handle partial sends if necessary (e.g., retry sending remaining data)
    }

    return true;
}


void BackchannelSink::afterGettingFrame(void* clientData, unsigned frameSize,
                                      unsigned numTruncatedBytes,
                                      struct timeval presentationTime,
                                      unsigned durationInMicroseconds) {
    BackchannelSink* sink = (BackchannelSink*)clientData;
    sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

// The main data processing function
void BackchannelSink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
                                      struct timeval presentationTime, unsigned /*durationInMicroseconds*/) {
    if (numTruncatedBytes > 0) {
        LOG_WARN("Received truncated frame (" << frameSize << " bytes, " << numTruncatedBytes << " truncated). Discarding.");
        // Request the next frame anyway
        if (source() != NULL) {
            source()->getNextFrame(fReceiveBuffer, kReceiveBufferSize,
                                   afterGettingFrame, this,
                                   onSourceClosure, this);
        }
        return;
    }

    // Basic RTP header check (optional but recommended)
    // Note: Assumes fReceiveBuffer contains the full RTP packet
    if (frameSize < 12) { // Minimum RTP header size
        LOG_WARN("Received frame too small to be RTP (" << frameSize << " bytes). Discarding.");
    } else {
        // Extract payload type from RTP header (byte 1, bits 0-6)
        unsigned char rtpPayloadType = fReceiveBuffer[1] & 0x7F;

        if (rtpPayloadType != fRtpPayloadFormat) {
            LOG_WARN("Received frame with unexpected payload type: " << (int)rtpPayloadType
                     << " (expected: " << (int)fRtpPayloadFormat << "). Discarding.");
        } else {
            // Payload starts after the header (12 bytes) + CSRC list (variable)
            // For simplicity, assume no CSRCs (header size = 12)
            unsigned headerSize = 12;
            // A more robust implementation would parse the CC field (byte 0, bits 0-3)
            // unsigned numCSRCs = fReceiveBuffer[0] & 0x0F;
            // headerSize += numCSRCs * 4;

            if (frameSize <= headerSize) {
                 LOG_WARN("Received frame has no payload (size " << frameSize << ", header " << headerSize << "). Discarding.");
            } else {
                u_int8_t* payload = fReceiveBuffer + headerSize;
                unsigned payloadSize = frameSize - headerSize;

                // LOG_DEBUG("Received A-law frame: " << payloadSize << " bytes"); // Verbose

                // Send the raw A-law payload over TCP
                if (!sendData(payload, payloadSize)) {
                    // Error already logged in sendData
                    // Consider adding retry logic or stopping if errors persist
                }
            }
        }
    }

    // Request the next frame
    if (source() != NULL) {
        source()->getNextFrame(fReceiveBuffer, kReceiveBufferSize,
                               afterGettingFrame, this,
                               onSourceClosure, this);
    }
}

bool BackchannelSink::continuePlaying() {
    if (fSource == NULL) return false; // Should not happen if started correctly

    // Ensure TCP connection is attempted if not already connected
    if (!fIsConnected) {
        if (!connectToTarget()) {
            LOG_ERROR("Initial connection failed. Cannot start playing.");
            // Optionally schedule a retry?
            return false; // Cannot proceed without connection
        }
    }

    // It's necessary to start reading data from our source
    fSource->getNextFrame(fReceiveBuffer, kReceiveBufferSize,
                          afterGettingFrame, this,
                          onSourceClosure, this);

    return true; // Indicates we have started reading data
}
