#include "RTPPacketValidatorFilter.hpp"
#include "IMPBackchannel.hpp" // For format mapping
#include <FramedSource.hh>
#include <memory.h> // For memmove

#define MODULE "RTPValidator"

// Static wrapper used with task scheduler to resume getting frames
static void staticResumeGettingFrames(void* clientData) {
    RTPPacketValidatorFilter* filter = (RTPPacketValidatorFilter*)clientData;
    if (filter) {
        // Call the static FramedSource::handleTimeout function.
        // This function is designed to be called from task callbacks
        // and will appropriately resume frame reading for the filter.
        filter->doGetNextFrame();
    }
}

RTPPacketValidatorFilter* RTPPacketValidatorFilter::createNew(UsageEnvironment& env, FramedSource* inputSource) {
    return new RTPPacketValidatorFilter(env, inputSource);
}

RTPPacketValidatorFilter::RTPPacketValidatorFilter(UsageEnvironment& env, FramedSource* inputSource)
    : FramedFilter(env, inputSource), fBuffer(nullptr), fExpectedPayloadType(0) {
    fBuffer = new unsigned char[RTP_VALIDATOR_FILTER_BUFFER_SIZE];
    if (fBuffer == nullptr) {
        LOG_ERROR("Failed to allocate buffer for RTPPacketValidatorFilter");
        // Handle allocation failure?
    }

    // Try to get the expected payload type from the input source
    // This assumes the source was configured correctly (e.g., for PCMA PT 8)
    RTPSource* rtpSource = dynamic_cast<RTPSource*>(inputSource);
    if (rtpSource) {
        fExpectedPayloadType = rtpSource->rtpPayloadFormat();
        LOG_DEBUG("RTPPacketValidatorFilter initialized. Expecting Payload Type: " << fExpectedPayloadType);
    } else {
        LOG_WARN("RTPPacketValidatorFilter input source is not an RTPSource, cannot determine expected payload type automatically.");
        // Defaulting to PCMA for now, might need adjustment
        fExpectedPayloadType = IMPBackchannel::rtpPayloadTypeFromFormat(IMPBackchannelFormat::PCMA);
        LOG_WARN("Defaulting expected Payload Type to: " << fExpectedPayloadType);
    }
}

RTPPacketValidatorFilter::~RTPPacketValidatorFilter() {
    delete[] fBuffer;
}

void RTPPacketValidatorFilter::doGetNextFrame() {
    // Request data from our input source, delivering it into our buffer
    if (fInputSource) {
        fInputSource->getNextFrame(fBuffer, RTP_VALIDATOR_FILTER_BUFFER_SIZE,
                                   afterGettingFrame, this,
                                   FramedSource::handleClosure, this); // Use base class handler for closure
    }
}

void RTPPacketValidatorFilter::doStopGettingFrames() {
    // Pass the stop request down to our input source
    if (fInputSource) {
        fInputSource->stopGettingFrames();
    }
    // Note: We might also need to cancel pending tasks if any were added
    FramedFilter::doStopGettingFrames(); // Call base class implementation
}

void RTPPacketValidatorFilter::afterGettingFrame(void* clientData, unsigned frameSize,
                                                 unsigned numTruncatedBytes,
                                                 struct timeval presentationTime,
                                                 unsigned durationInMicroseconds) {
    RTPPacketValidatorFilter* filter = (RTPPacketValidatorFilter*)clientData;
    filter->afterGettingFrame1(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

void RTPPacketValidatorFilter::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                                                  struct timeval presentationTime,
                                                  unsigned durationInMicroseconds) {
    Boolean isValidRTP = false;
    unsigned char receivedPayloadType = 0xFF; // Initialize to invalid

    // Basic validation checks
    if (numTruncatedBytes > 0) {
        LOG_WARN("RTPValidator: Received truncated frame (" << frameSize << " bytes, " << numTruncatedBytes << " truncated). Discarding.");
        // Request next frame immediately
        doGetNextFrame();
        return;
    }

    if (frameSize >= 12) { // Need at least 12 bytes for basic RTP header
        // Check RTP Version (should be 2)
        unsigned char version = (fBuffer[0] >> 6) & 0x03;
        // Extract Payload Type
        receivedPayloadType = fBuffer[1] & 0x7F;

        if (version == 2) {
            // Check if payload type matches expected (if known)
             if (receivedPayloadType == fExpectedPayloadType) {
                 isValidRTP = true;
             } else {
                 // Removed std::hex
                 // Add frameSize to log
                 LOG_WARN("RTPValidator: Received packet (size " << frameSize << ") with unexpected Payload Type: " << (int)receivedPayloadType << " (expected: " << fExpectedPayloadType << "). Header: " << (int)fBuffer[0] << " " << (int)fBuffer[1] << ". Discarding.");
             }
         } else {
             // Removed std::hex
             // Add frameSize to log
             LOG_WARN("RTPValidator: Received packet (size " << frameSize << ") with invalid RTP Version: " << (int)version << ". Header: " << (int)fBuffer[0] << " " << (int)fBuffer[1] << ". Discarding.");
         }
     } else {
         // Add frameSize to log
         LOG_WARN("RTPValidator: Received frame smaller than RTP header (" << frameSize << " bytes). Discarding.");
     }

    if (isValidRTP) {
        // The packet looks like valid RTP with the expected payload type.
        // Pass it downstream to the sink.

        // Ensure our output buffer (fTo) is large enough
        if (frameSize > fMaxSize) {
            fFrameSize = fMaxSize;
            fNumTruncatedBytes = frameSize - fMaxSize;
            LOG_WARN("RTPValidator: Frame truncated downstream (size " << frameSize << " > max " << fMaxSize << ")");
        } else {
            fFrameSize = frameSize;
            fNumTruncatedBytes = 0;
        }

        // Copy the validated frame data to the output buffer
        memmove(fTo, fBuffer, fFrameSize);
        fPresentationTime = presentationTime;
        fDurationInMicroseconds = durationInMicroseconds;

        // Notify the downstream sink/filter that we have data
        // (This schedules doGetNextFrame() for the downstream object)
        FramedSource::afterGetting(this);

    } else {
        // The packet was invalid (bad header, wrong PT, too small) or truncated upstream.
         // Discard it and immediately request the next frame from our input source.
         // (Error/Warning was already logged above)
         // Use post-call scheduling with the static wrapper function that calls handleTimeout.
         envir().taskScheduler().scheduleDelayedTask(0, (TaskFunc*)staticResumeGettingFrames, this);
     }
 }
