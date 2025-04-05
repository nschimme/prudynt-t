#include "BackchannelSink.hpp"
#include "Logger.hpp"
#include "globals.hpp" // For backchannel_stream

// Standard Headers
#include <sys/time.h> // For struct timeval used in afterGettingFrame signature

// Live555 Headers
#include <RTPSource.hh>
#include <vector>
#include <atomic> // Include atomic header
#include <arpa/inet.h> // For ntohl

#define MODULE "BackchannelSink"

// Basic RTP header structure (RFC 3550)
typedef struct {
#if __BYTE_ORDER == __BIG_ENDIAN
    unsigned int version:2;   /* protocol version */
    unsigned int p:1;         /* padding flag */
    unsigned int x:1;         /* header extension flag */
    unsigned int cc:4;        /* CSRC count */
    unsigned int m:1;         /* marker bit */
    unsigned int pt:7;        /* payload type */
#elif __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned int cc:4;        /* CSRC count */
    unsigned int x:1;         /* header extension flag */
    unsigned int p:1;         /* padding flag */
    unsigned int version:2;   /* protocol version */
    unsigned int pt:7;        /* payload type */
    unsigned int m:1;         /* marker bit */
#else
#error "Define byte order for your platform"
#endif
    unsigned int seq:16;      /* sequence number */
    uint32_t ts;              /* timestamp */
    uint32_t ssrc;            /* synchronization source */
    // uint32_t csrc[1];      /* optional CSRC list */ // We assume cc=0 for simplicity
} rtp_hdr_t;

const unsigned RTP_HEADER_SIZE = 12; // Base RTP header size without CSRCs

// Initialize static members
std::atomic<bool> BackchannelSink::gIsAudioOutputBusy{false}; // Define and initialize static atomic flag

// Reverted createNew signature
BackchannelSink* BackchannelSink::createNew(UsageEnvironment& env, backchannel_stream* stream_data,
                                            unsigned clientSessionId) {
    return new BackchannelSink(env, stream_data, clientSessionId);
}

// Reverted constructor signature and initialization list (reordered to match declaration)
BackchannelSink::BackchannelSink(UsageEnvironment& env, backchannel_stream* stream_data,
                                 unsigned clientSessionId)
    : MediaSink(env), // Base class constructor
      fRTPSource(nullptr),
      // fReceiveBuffer allocated below
      fStream(stream_data),
      fInputQueue(nullptr),
      fIsActive(False),
      fAfterFunc(nullptr),
      fAfterClientData(nullptr),
      fHaveAudioOutputLock(false), // Initialize lock state
      fClientSessionId(clientSessionId), // Initialize client session ID
      fTimeoutTask(nullptr),             // Initialize timeout task token
      fFormat(IMPBackchannelFormat::UNKNOWN) // Initialize format as unknown
      // fFrequency removed
{
    LOG_DEBUG("BackchannelSink created for client session " << fClientSessionId); // Removed format/freq log
    // gettimeofday(&fLastDataTime, nullptr); // Removed: Not needed
    if (fStream == nullptr) {
         LOG_ERROR("backchannel_stream provided to BackchannelSink is null! (Session: " << fClientSessionId << ")"); // Added session ID
         // Handle error - maybe assert?
    } else {
        // Note: fInputQueue type is now MsgChannel<BackchannelFrame>*
        fInputQueue = fStream->inputQueue.get();
        if (fInputQueue == nullptr) {
             LOG_ERROR("Input queue within backchannel_stream is null! (Session: " << fClientSessionId << ")"); // Added session ID
             // Handle error
        }
    }
    fReceiveBuffer = new u_int8_t[kReceiveBufferSize];
    if (fReceiveBuffer == nullptr) {
        LOG_ERROR("Failed to allocate receive buffer (Session: " << fClientSessionId << ")"); // Added session ID
        // Consider throwing an exception or handling error more robustly
    }
}

BackchannelSink::~BackchannelSink() {
    LOG_DEBUG("BackchannelSink destroyed for client session " << fClientSessionId); // Added session ID
    // Ensure we stop playing if active (should be stopped by StreamState already)
    // Also release lock if held and cancel timer
    stopPlaying();
    delete[] fReceiveBuffer;
    // fInputQueue and fStream are managed externally
}

Boolean BackchannelSink::startPlaying(RTPSource& rtpSource,
                                      MediaSink::afterPlayingFunc* afterFunc,
                                      void* afterClientData)
{
    // Lock acquisition moved to afterGettingFrame1

    if (fIsActive) {
        LOG_WARN("startPlaying called while already active (fIsActive=True) for session " << fClientSessionId << ". This might indicate an issue."); // Added session ID
        return False;
    }

    fRTPSource = &rtpSource;
    fAfterFunc = afterFunc;
    fAfterClientData = afterClientData;
    fHaveAudioOutputLock = false; // Ensure lock is initially not held
    fIsActive = True; // Mark as active

    // Increment active session count using fStream (atomically)
    if (fStream) {
        int previous_count = fStream->active_sessions.fetch_add(1, std::memory_order_relaxed);
        LOG_INFO("Backchannel sink starting for session " << fClientSessionId << ". Active sessions: " << previous_count + 1); // Added session ID
    } else {
        LOG_ERROR("fStream is null in startPlaying! Cannot increment session count. (Session: " << fClientSessionId << ")"); // Added session ID
    }

    // Record start time. Timer is NOT scheduled here anymore.
    // gettimeofday(&fLastDataTime, nullptr); // Removed: Not needed
    // scheduleTimeoutCheck(); // Removed: Timer starts only after first frame + lock acquisition

    LOG_INFO("BackchannelSink starting to play/consume from RTPSource for session " << fClientSessionId); // Added session ID
    // Start the process by requesting the first frame
    return continuePlaying();
}

void BackchannelSink::stopPlaying() {
    if (!fIsActive) {
        // LOG_DEBUG("stopPlaying called while not active for session " << fClientSessionId); // Can be noisy
        return;
    }

    LOG_INFO("BackchannelSink stopping play/consumption for session " << fClientSessionId); // Added session ID

    // Cancel any pending timeout timer FIRST
    envir().taskScheduler().unscheduleDelayedTask(fTimeoutTask);
    fTimeoutTask = nullptr;

    fIsActive = False;

    // Stop the source from delivering frames
    if (fRTPSource != nullptr) {
        fRTPSource->stopGettingFrames();
    }

    // --- Lock Release Logic (if held) ---
    if (fHaveAudioOutputLock) {
        gIsAudioOutputBusy.store(false); // Atomically release the lock
        fHaveAudioOutputLock = false;    // Update instance state
        LOG_INFO("BackchannelSink released audio output lock for session " << fClientSessionId << " during stopPlaying."); // Added session ID
        // Add IMP AOUT stop/deinit logic here if needed
    }
    // --- End Lock Release ---

    // Decrement active session count (atomically)
    if (fStream) {
        int previous_count = fStream->active_sessions.fetch_sub(1, std::memory_order_relaxed);
        LOG_INFO("Backchannel sink stopped for session " << fClientSessionId << ". Active sessions: " << previous_count - 1); // Added session ID
         // Optional: Add logic here if previous_count was 1 (last session ended)
    } else {
         LOG_ERROR("fStream is null in stopPlaying! Cannot decrement session count. (Session: " << fClientSessionId << ")"); // Added session ID
    }

    // Call the afterPlaying function if specified
    if (fAfterFunc != nullptr) {
        (*fAfterFunc)(fAfterClientData);
    }

    // Clear pointers after stopping
    fRTPSource = nullptr;
    fAfterFunc = nullptr;
    fAfterClientData = nullptr;
}

// Getter implementation
unsigned BackchannelSink::getClientSessionId() const {
    return fClientSessionId;
}


// Called by Live555 when the source has data or closes
Boolean BackchannelSink::continuePlaying() {
    if (!fIsActive || fRTPSource == nullptr) {
        return False; // Not active or source detached
    }

    // Request the next frame from the source
    fRTPSource->getNextFrame(fReceiveBuffer, kReceiveBufferSize,
                             afterGettingFrame, this,
                             onSourceClosure, this); // Use static wrappers

    return True; // Indicate we want to continue
}

// Static callback wrapper for incoming frames from the RTPSource
void BackchannelSink::afterGettingFrame(void* clientData, unsigned frameSize,
                                        unsigned numTruncatedBytes,
                                         struct timeval presentationTime,
                                         unsigned /*durationInMicroseconds*/) // Duration often 0 for RTP
{
    BackchannelSink* sink = static_cast<BackchannelSink*>(clientData);
    if (sink != nullptr) {
        sink->afterGettingFrame1(frameSize, numTruncatedBytes, presentationTime);
    } else {
        LOG_ERROR("afterGettingFrame called with invalid clientData");
    }
} // Added missing closing brace


// Per-instance handler for incoming frames
void BackchannelSink::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                                         struct timeval presentationTime) // Removed unused variable marker
{
    if (!fIsActive) {
        // LOG_DEBUG("Frame received but sink is no longer active for session " << fClientSessionId); // Added session ID
        return; // No longer playing
    }

    // Update last data time whenever a frame arrives (even 0-size or truncated)
    // gettimeofday(&fLastDataTime, nullptr); // Removed: Not needed

    // Attempt to acquire lock if we don't have it and received a valid frame
    if (frameSize > 0 && numTruncatedBytes == 0 && !fHaveAudioOutputLock) {
        bool expected = false;
        if (gIsAudioOutputBusy.compare_exchange_strong(expected, true)) {
            // Successfully acquired the lock
            fHaveAudioOutputLock = true;
            LOG_INFO("BackchannelSink acquired audio output lock for session " << fClientSessionId << " upon receiving first frame."); // Added session ID
            // Proceed with initializing audio playback... (Add IMP AOUT init here if needed)

            // Start the audio data timeout timer ONLY after acquiring the lock for the first time
            LOG_DEBUG("Starting audio data timeout timer for session " << fClientSessionId);
            scheduleTimeoutCheck();

        } else {
            // Lock was already held by another instance
            fHaveAudioOutputLock = false; // Ensure our state is correct
            LOG_WARN("BackchannelSink failed to acquire audio output lock for session " << fClientSessionId << " (already busy). Will receive data but not play."); // Added session ID
        }
    }

    // Process the received frame
    if (numTruncatedBytes > 0) {
        LOG_WARN("Received truncated frame (" << frameSize << " bytes, " << numTruncatedBytes << " truncated) for session " << fClientSessionId << ". Discarding."); // Added session ID
    } else if (frameSize < RTP_HEADER_SIZE) { // Check if frame is large enough for RTP header
        LOG_WARN("Received frame smaller than RTP header (" << frameSize << " bytes) for session " << fClientSessionId << ". Discarding.");
    } else if (frameSize > 0) {
        // Valid frame received, parse RTP header and create BackchannelFrame
        rtp_hdr_t* rtp_header = (rtp_hdr_t*)fReceiveBuffer;

        // Determine format from payload type on the first valid packet
        if (fFormat == IMPBackchannelFormat::UNKNOWN) {
            unsigned char payloadType = rtp_header->pt;
            // Use helper function to map RTP PT to our enum
            fFormat = IMPBackchannel::formatFromRtpPayloadType(payloadType);

            if (fFormat == IMPBackchannelFormat::UNKNOWN) {
                // Error already logged by helper function
                // Keep fFormat as UNKNOWN and potentially stop processing? Or just discard this packet?
                // For now, just discard this packet by returning.
                // Consider adding logic to tear down the stream if format is unsupported.
                return; // Skip processing this packet
            }
            LOG_INFO("Determined backchannel format for session " << fClientSessionId << " as " << static_cast<int>(fFormat) << " (PT=" << (int)payloadType << ")");
        }

        uint32_t rtpTimestamp = ntohl(rtp_header->ts); // Convert from network byte order

        // Calculate presentation time based on RTP timestamp and frequency
        // Note: presentationTime passed to this function is from Live555's internal clock,
        // using the RTP timestamp might be more accurate for audio sync if clocks differ.
        struct timeval frameTimestamp;
        // TODO: Need a more robust way to map RTP timestamp to struct timeval, potentially
        // using RTCP SR packets or initial packet arrival time as a reference.
        // Using presentationTime for now as a fallback, as the calculation based on
        // rtpTimestamp and fFrequency was complex and potentially inaccurate without
        // proper clock synchronization handling.
        frameTimestamp = presentationTime; // Use Live555's presentationTime

        unsigned payloadSize = frameSize - RTP_HEADER_SIZE; // Basic calculation
        uint8_t* payloadData = fReceiveBuffer + RTP_HEADER_SIZE;

        BackchannelFrame bcFrame;
        bcFrame.format = fFormat;                   // Use determined format
        // bcFrame.frequency removed
        bcFrame.timestamp = frameTimestamp;         // Use presentation timestamp
        bcFrame.payload.assign(payloadData, payloadData + payloadSize);

        // Only process/queue if we hold the audio lock
        if (fHaveAudioOutputLock) {
            // LOG_DEBUG("Received frame, queuing BackchannelFrame for session " << fClientSessionId); // Can be very noisy
            if (fInputQueue) {
                if (!fInputQueue->write(std::move(bcFrame))) {
                     LOG_WARN("Backchannel input queue was full for session " << fClientSessionId << ". Oldest frame dropped."); // Added session ID
                }
            } else {
                 LOG_ERROR("Input queue is null, cannot queue BackchannelFrame! (Session: " << fClientSessionId << ")"); // Added session ID
            }
        } else {
            // We don't have the lock, discard the data silently or log optionally
            // LOG_DEBUG("Discarding incoming backchannel frame for session " << fClientSessionId << ", audio output lock not held."); // Added session ID
        }
    }

    // Reset the audio data timeout timer whenever a frame arrives (if it was started)
    if (fTimeoutTask != nullptr || fHaveAudioOutputLock) { // Only reset if timer is running or lock is held (timer should be running if lock is held)
        envir().taskScheduler().unscheduleDelayedTask(fTimeoutTask);
        scheduleTimeoutCheck();
    }

    // Immediately request the next frame to keep the data flowing
    if (fIsActive) {
        continuePlaying();
    }
}


// --- Audio Data Timeout Timer Logic (Simplified) ---

void BackchannelSink::scheduleTimeoutCheck() {
    // Schedule the timer
    fTimeoutTask = envir().taskScheduler().scheduleDelayedTask(
        kAudioDataTimeoutSeconds * 1000000, // delay in microseconds
        (TaskFunc*)timeoutCheck,
        this);
}

void BackchannelSink::timeoutCheck(void* clientData) {
    BackchannelSink* sink = static_cast<BackchannelSink*>(clientData);
    if (sink) {
        sink->timeoutCheck1();
    }
}

void BackchannelSink::timeoutCheck1() {
    fTimeoutTask = nullptr; // Task is firing, clear the token

    if (!fIsActive) {
        // LOG_DEBUG("Timeout check fired but sink is no longer active for session " << fClientSessionId);
        return;
    }

    // When this timer callback fires, it means kAudioDataTimeoutSeconds have passed
    // since the last call to scheduleTimeoutCheck (which happens on frame arrival).
    // No need to check timestamps explicitly.

    LOG_WARN("Audio data timeout detected for session " << fClientSessionId << " (>" << kAudioDataTimeoutSeconds << "s).");

    if (fHaveAudioOutputLock) {
        LOG_WARN("Releasing audio output lock due to audio data timeout for session " << fClientSessionId << ".");
        gIsAudioOutputBusy.store(false); // Atomically release the global lock
        fHaveAudioOutputLock = false;    // Update instance state
        // Add IMP AOUT stop/deinit logic here if needed
    } else {
        // LOG_DEBUG("Audio data timeout detected for session " << fClientSessionId << ", but lock was not held.");
    }
    // Do NOT reschedule the timer here. It will only be rescheduled
    // if/when new data arrives via afterGettingFrame1.
}

// --- End Audio Data Timeout Timer Logic ---


// Static callback for source closure (called by Live555 via getNextFrame)
void MediaSink::onSourceClosure(void* clientData) {
    BackchannelSink* sink = static_cast<BackchannelSink*>(clientData);
    if (sink != nullptr) {
        // Use the getter function to access the client session ID
        LOG_INFO("Source closure detected by BackchannelSink for session " << sink->getClientSessionId());
        // The source has closed (e.g., client disconnected sending RTCP BYE)
        // We should stop playing, which will also call the afterPlaying function if set.
        // Note: Don't call stopPlaying directly from here if it might delete the sink
        //       or cause re-entrancy issues. Schedule it instead.
        sink->envir().taskScheduler().scheduleDelayedTask(0,
            (TaskFunc*)[](void* cd) {
                BackchannelSink* s = static_cast<BackchannelSink*>(cd);
                if (s) s->stopPlaying(); // stopPlaying now handles timer cancellation and lock release
            },
            sink);
    } else {
         LOG_ERROR("onSourceClosure called with invalid clientData");
    }
}
