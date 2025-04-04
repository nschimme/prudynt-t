// Project Headers
#include "BackchannelSink.hpp"
#include "Logger.hpp"

// Live555 Headers
#include "Media.hh" // For Medium::close

// Standard C/C++ Headers
#include <stdio.h>    // For popen, pclose, fwrite, ferror, feof
#include <errno.h>    // For errno
#include <string.h>   // For strerror
#include <sys/wait.h> // For WEXITSTATUS, WIFEXITED, WIFSIGNALED, WTERMSIG
#include <algorithm>  // Potentially needed for list operations if changed later

#define MODULE "BackchannelSink"

BackchannelSink* BackchannelSink::createNew(UsageEnvironment& env) {
    return new BackchannelSink(env);
}

BackchannelSink::BackchannelSink(UsageEnvironment& env)
    : MediaSink(env),
      fControllingSource(nullptr), // Initialize controlling source
      fPipe(nullptr), // Initialize pipe pointer
      fIsPlaying(false) // Initialize playing state
{
    fReceiveBuffer = new u_int8_t[kReceiveBufferSize];
    if (fReceiveBuffer == nullptr) {
        LOG_ERROR("Failed to allocate receive buffer");
        // Handle error appropriately, maybe throw?
    }
    // Note: Pipe is started lazily in continuePlaying()
}

BackchannelSink::~BackchannelSink() {
    // Clean up contexts
    for (auto const& [source, context] : fSourceContexts) {
        delete context;
    }
    fSourceContexts.clear();
    fClientSources.clear(); // Clear the list too

    delete[] fReceiveBuffer;
    closePipe(); // Ensure pipe is closed on destruction
}

void BackchannelSink::stopPlaying() {
    LOG_INFO("Stopping playback.");
    fIsPlaying = false; // Set playing state to false
    // Stop getting frames from the current controlling source
    if (fControllingSource && fSource == fControllingSource) {
         fControllingSource->stopGettingFrames(); // Use base class fSource mechanism
    }
    fControllingSource = nullptr; // Clear the controller
    fSource = nullptr; // Clear base class source pointer
    MediaSink::stopPlaying(); // Call base class stop
    closePipe(); // Close the pipe when stopping
}

bool BackchannelSink::startPipe() {
    if (fPipe != NULL) {
        return true; // Already open
    }

    LOG_INFO("Starting pipe to /bin/iac -s");
    fPipe = popen("/bin/iac -s", "w"); // Open pipe for writing
    if (fPipe == NULL) {
        LOG_ERROR("popen() failed: " << strerror(errno));
        return false;
    }
    LOG_INFO("Pipe to /bin/iac -s started successfully.");
    return true;
}

void BackchannelSink::closePipe() {
    if (fPipe != NULL) {
        LOG_INFO("Closing pipe to /bin/iac -s");
        int ret = pclose(fPipe);
        fPipe = NULL; // Set to NULL regardless of pclose result
        if (ret == -1) {
            LOG_ERROR("pclose() failed: " << strerror(errno));
        } else {
             // Check if the process exited normally
             if (WIFEXITED(ret)) {
                 LOG_INFO("Pipe closed. Process exited with status: " << WEXITSTATUS(ret));
             } else if (WIFSIGNALED(ret)) {
                 LOG_WARN("Pipe closed. Process terminated by signal: " << WTERMSIG(ret));
             } else {
                 LOG_WARN("Pipe closed. Process stopped for unknown reason.");
             }
        }
    }
}

bool BackchannelSink::writeToPipe(u_int8_t* data, unsigned size) {
    if (fPipe == NULL) {
         LOG_ERROR("Attempted to write to a closed or uninitialized pipe.");
         return false;
    }

    size_t bytesWritten = fwrite(data, 1, size, fPipe);
    if (bytesWritten < size) {
        // Check error status *before* logging strerror, as logging might change errno
        int stream_error = ferror(fPipe);
        int stream_eof = feof(fPipe);
        int saved_errno = errno; // Save errno immediately

        if (stream_eof) {
             LOG_ERROR("fwrite failed: End of file on pipe (process likely terminated). Closing pipe.");
        } else if (stream_error) {
             // Use saved_errno if ferror is set, otherwise strerror might report success
             LOG_ERROR("fwrite failed: " << strerror(saved_errno) << ". Closing pipe.");
        } else {
             LOG_ERROR("fwrite failed: Wrote partial data (" << bytesWritten << " of " << size << " bytes). Unknown error. Closing pipe.");
        }
        // Close the pipe on any write error
        closePipe();
        return false;
    }
    // Optional: fflush(fPipe); // Uncomment if immediate flushing is needed, might impact performance

    return true;
}


void BackchannelSink::addSource(FramedSource* newSource) {
    if (newSource == nullptr) return;

    LOG_INFO("Adding source");

    // Stop previous controlling source from getting frames if it exists
    // Note: This assumes stopGettingFrames() is safe to call multiple times
    // or if no getNextFrame is pending. Live555 might handle this.
    if (fControllingSource != nullptr && fSource == fControllingSource) {
         LOG_DEBUG("Stopping previous controlling source");
         fControllingSource->stopGettingFrames();
    }

    // Add to the list (maintains order for fallback)
    fClientSources.push_back(newSource);

    // Create and store context
    BackchannelClientData* context = new BackchannelClientData{this, newSource};
    fSourceContexts[newSource] = context;

    // New source takes control
    fControllingSource = newSource;
    fSource = newSource; // Update base class pointer

    LOG_INFO("New controlling source");

    // If we are already playing, start getting frames from the new controller
    if (fIsPlaying) { // Use our state variable
        LOG_INFO("Sink is playing, requesting frames from new controlling source.");
        fControllingSource->getNextFrame(fReceiveBuffer, kReceiveBufferSize,
                                         afterGettingFrame, context,
                                         onSourceClosure, this);
    }
}

void BackchannelSink::removeSource(FramedSource* sourceToRemove) {
    if (sourceToRemove == nullptr) return;

    LOG_INFO("Removing source");

    // Find and remove from the list
    fClientSources.remove(sourceToRemove);

    // Find and delete the context
    auto it = fSourceContexts.find(sourceToRemove);
    if (it != fSourceContexts.end()) {
        delete it->second;
        fSourceContexts.erase(it);
    } else {
        LOG_WARN("Attempted to remove source not in context map");
    }

    // If the removed source was the controller, fall back
    if (sourceToRemove == fControllingSource) {
        LOG_INFO("Removed source was the controller.");
        // Stop getting frames from the removed source
        if (fSource == sourceToRemove) {
             sourceToRemove->stopGettingFrames();
        }

        if (!fClientSources.empty()) {
            fControllingSource = fClientSources.back(); // Fallback to the last one in the list
            fSource = fControllingSource; // Update base class pointer
            LOG_INFO("Falling back to controlling source");

            // If playing, start getting frames from the new controller
            if (this->fIsPlaying) { // Use our state variable
                 auto fallbackIt = fSourceContexts.find(fControllingSource);
                 if (fallbackIt != fSourceContexts.end()) {
                     LOG_INFO("Sink is playing, requesting frames from new fallback controlling source.");
                     fControllingSource->getNextFrame(fReceiveBuffer, kReceiveBufferSize,
                                                      afterGettingFrame, fallbackIt->second,
                                                      onSourceClosure, this);
                 } else {
                     LOG_ERROR("Fallback source has no context!");
                     fControllingSource = nullptr; // Cannot proceed
                     fSource = nullptr;
                 }
            }
        } else {
            LOG_INFO("No sources left after removal.");
            fControllingSource = nullptr;
            fSource = nullptr;
        }
    } else {
         // If removing a non-controlling source, just ensure it stops getting frames
         // (though it shouldn't have been getting them anyway based on afterGettingFrame logic)
         LOG_DEBUG("Removed source was not the controller.");
         sourceToRemove->stopGettingFrames(); // Belt-and-suspenders
    }
}


// Static wrapper
void BackchannelSink::afterGettingFrame(void* clientData, unsigned frameSize,
                                      unsigned numTruncatedBytes,
                                      struct timeval presentationTime,
                                      unsigned durationInMicroseconds) {
    BackchannelClientData* context = static_cast<BackchannelClientData*>(clientData);
    if (context && context->sink) {
        context->sink->afterGettingFrame(context, frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
    }
    // else {
        // Log error or handle appropriately if context is invalid
        // This might happen if the sink is destroyed while a frame callback is pending
    // }
}

// Per-instance handler using the context
void BackchannelSink::afterGettingFrame(BackchannelClientData* context, unsigned frameSize, unsigned numTruncatedBytes,
                                      struct timeval /*presentationTime*/, unsigned /*durationInMicroseconds*/) {

    FramedSource* currentSource = context->source;

    // --- Critical Section: Check if this source is the controller ---
    // No mutex needed here as fControllingSource is only written in add/removeSource,
    // which likely happen in the main thread context, not concurrently with this callback.
    // If add/remove can happen in different threads, a mutex would be needed around fControllingSource reads/writes.
    if (currentSource != fControllingSource) {
        // Ignore frame from non-controlling source
        // Do NOT request the next frame for this non-controlling source
        return;
    }
    // --- End Critical Section ---

    // If we get here, currentSource == fControllingSource

    if (fPipe == nullptr) {
        LOG_ERROR("Pipe not open in afterGettingFrame for controlling source. Stopping.");
        // Don't request next frame
        return;
    }

    if (numTruncatedBytes > 0) {
        LOG_WARN("Controlling source sent truncated frame (" << frameSize << " bytes, " << numTruncatedBytes << " truncated). Discarding.");
        // Continue playing - request next frame below
    } else if (frameSize < 12) { // Basic RTP header check
        LOG_WARN("Controlling source sent frame too small for RTP (" << frameSize << " bytes). Discarding.");
    } else {
        // Extract payload type from RTP header
        unsigned char rtpPayloadType = fReceiveBuffer[1] & 0x7F;
        const unsigned char expectedPayloadFormat = 8; // Hardcoded payload format for PCMA/ALAW

        if (rtpPayloadType != expectedPayloadFormat) {
            LOG_WARN("Controlling source sent frame with unexpected payload type: " << (int)rtpPayloadType
                     << " (expected: " << (int)expectedPayloadFormat << "). Discarding.");
        } else {
            // Assuming fixed 12-byte RTP header
            // TODO: Handle RTP header extensions and CSRC list if present (RFC 3550)
            unsigned headerSize = 12; // Basic header size
            // unsigned char cc = fReceiveBuffer[0] & 0x0F; // CSRC count
            // headerSize += cc * 4;
            // bool hasExtension = (fReceiveBuffer[0] & 0x10);
            // if (hasExtension && frameSize >= headerSize + 4) {
            //     unsigned extensionLen = (fReceiveBuffer[headerSize+2] << 8) | fReceiveBuffer[headerSize+3];
            //     headerSize += 4 + extensionLen * 4;
            // }


            if (frameSize <= headerSize) {
                 LOG_WARN("Controlling source sent frame with no payload (size " << frameSize << ", header " << headerSize << "). Discarding.");
            } else {
                u_int8_t* payload = fReceiveBuffer + headerSize;
                unsigned payloadSize = frameSize - headerSize;

                // Write the raw payload data to the pipe
                if (!writeToPipe(payload, payloadSize)) {
                    // Error logged in writeToPipe. Pipe is likely closed now.
                    LOG_ERROR("Write to pipe failed for controlling source. Stopping playback.");
                    // Don't request next frame if pipe failed
                    return;
                }
            }
        }
    }

    // Request the next frame *only* from the controlling source
    if (fPipe != nullptr && fControllingSource == currentSource) {
        fControllingSource->getNextFrame(fReceiveBuffer, kReceiveBufferSize,
                                         afterGettingFrame, context, // Pass the same context back
                                         onSourceClosure, this);
    } else if (fPipe == nullptr) {
         LOG_INFO("Pipe closed, not requesting further frames");
    } else {
         // This case (fControllingSource != currentSource) should have been caught at the top
         LOG_ERROR("Logic error: Reached end of afterGettingFrame for non-controlling source?");
    }
}


Boolean BackchannelSink::continuePlaying() {
    fIsPlaying = true; // Set playing state to true
    // Ensure the pipe is started before requesting the first frame
    if (!startPipe()) {
        LOG_ERROR("Failed to start pipe. Cannot continue playing.");
        return false; // Cannot proceed without the pipe
    }

    if (fControllingSource == nullptr) {
        LOG_WARN("No controlling source set, cannot continue playing yet.");
        // We might get added sources later, so return true, but don't request frames yet.
        // Or return false? Let's return true, assuming a source will be added.
        return true;
    }

    // Find the context for the controlling source
    auto it = fSourceContexts.find(fControllingSource);
    if (it == fSourceContexts.end()) {
         LOG_ERROR("Controlling source has no context! Cannot continue playing.");
         return false;
    }
    BackchannelClientData* context = it->second;

    // Request the first frame from the controlling source
    LOG_INFO("Requesting first frame from controlling source");
    fControllingSource->getNextFrame(fReceiveBuffer, kReceiveBufferSize,
                                     afterGettingFrame, context,
                                     onSourceClosure, this);

    // We rely on the base class fSource being set correctly in addSource/removeSource
    return true; // Indicate success
}
