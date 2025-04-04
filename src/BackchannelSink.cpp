#include "BackchannelSink.hpp" // Updated include
#include <GroupsockHelper.hh> // For closeSocket
#include <vector> // For std::vector

#define MODULE "BackchannelSink" // Updated module name

BackchannelSink* BackchannelSink::createNew(UsageEnvironment& env, backchannel_stream* stream_data) { // Updated signature
    return new BackchannelSink(env, stream_data);
}

BackchannelSink::BackchannelSink(UsageEnvironment& env, backchannel_stream* stream_data) // Updated signature
    : FramedSource(env), // Still inherits FramedSource
      fControllingSource(nullptr),
      fStream(stream_data), // Store the stream pointer
      fInputQueue(nullptr), // Initialize convenience pointer
      fIsCurrentlyGettingFrames(false) // Initialize flag
{
    if (fStream == nullptr) {
         LOG_ERROR("backchannel_stream provided to BackchannelSink is null!");
         // Handle error
    } else {
        fInputQueue = fStream->inputQueue.get(); // Get queue pointer from stream struct
        if (fInputQueue == nullptr) {
             LOG_ERROR("Input queue within backchannel_stream is null!");
             // Handle error
        }
    }
    fClientReceiveBuffer = new u_int8_t[kClientReceiveBufferSize];
    if (fClientReceiveBuffer == nullptr) {
        LOG_ERROR("Failed to allocate client receive buffer");
        // Consider throwing an exception or handling error more robustly
    }
}

BackchannelSink::~BackchannelSink() {
    // envir().taskScheduler().unscheduleDelayedTask(fNextTask); // Removed

    // Clean up contexts and stop sources
    while (!fClientSources.empty()) {
        FramedSource* source = fClientSources.front();
        // Call removeSource to handle cleanup logic consistently
        removeSource(source);
    }
    // Ensure map is clear (should be by removeSource calls)
    fSourceContexts.clear();

    delete[] fClientReceiveBuffer;
    // fInputQueue is managed externally
}

void BackchannelSink::addSource(FramedSource* newSource) { // Renamed class
    if (newSource == nullptr) return;

    LOG_INFO("Adding client source");

    // Stop previous controlling source if it exists and we are getting frames
    if (fControllingSource != nullptr && fIsCurrentlyGettingFrames) { // Use new flag
        LOG_DEBUG("Stopping previous controlling source");
        fControllingSource->stopGettingFrames();
    }

    // Add to the list
    fClientSources.push_back(newSource);

    // Create and store context (update 'this' type)
    ClientSourceContext* context = new ClientSourceContext{this, newSource};
    fSourceContexts[newSource] = context;

    // New source takes control. This sink intentionally only processes data
    // from one client source at a time (the "controlling source"). When a new
    // client connects, it becomes the controlling source, effectively implementing
    // a "last talker wins" strategy.
    fControllingSource = newSource;
    LOG_INFO("New controlling source set");

    // Increment active session count using fStream (atomically)
    if (fStream) { // Check if fStream is valid
        // Use fetch_add for atomic increment. Memory order relaxed is sufficient
        // as we only need atomicity, not specific ordering guarantees here.
        int previous_count = fStream->active_sessions.fetch_add(1, std::memory_order_relaxed);
        LOG_INFO("Backchannel session added. Active sessions: " << previous_count + 1);
        // If this is the first session, the processor thread will notice and open the pipe.
    } else {
        LOG_ERROR("fStream is null in addSource! Cannot increment session count.");
    }


    // If we should be getting frames, start getting from the new controller
    if (fIsCurrentlyGettingFrames) { // Use new flag
        LOG_INFO("Sink is active, requesting frames from new controlling source.");
        fControllingSource->getNextFrame(fClientReceiveBuffer, kClientReceiveBufferSize,
                                         incomingDataHandler, context,
                                         sourceClosureHandler, context); // Pass context for closure too
    }
}

void BackchannelSink::removeSource(FramedSource* sourceToRemove) { // Renamed class
    if (sourceToRemove == nullptr) return;

    LOG_INFO("Removing client source");

    // Find and remove from the list
    fClientSources.remove(sourceToRemove);

    // Find and delete the context
    auto it = fSourceContexts.find(sourceToRemove);
    if (it != fSourceContexts.end()) {
        // Stop getting frames *before* deleting context, just in case
        sourceToRemove->stopGettingFrames();

        // Decrement active session count BEFORE deleting context using fStream (atomically)
        if (fStream) { // Check if fStream is valid
            // Use fetch_sub for atomic decrement. Memory order relaxed is sufficient.
            int previous_count = fStream->active_sessions.fetch_sub(1, std::memory_order_relaxed);
            // Note: previous_count holds the value *before* the decrement.
            LOG_INFO("Backchannel session removed. Active sessions: " << previous_count - 1);
            // If this was the last session (i.e., previous_count was 1),
            // the processor thread will notice and close the pipe.
        } else {
             LOG_ERROR("fStream is null in removeSource! Cannot decrement session count.");
        }

        delete it->second; // Now delete context
        fSourceContexts.erase(it);
    } else {
        LOG_WARN("Attempted to remove source not in context map");
        // Decrement count even if context wasn't found? Maybe not, indicates inconsistency.
        // Still try to stop it if it might be active
        sourceToRemove->stopGettingFrames();
    }

    // If the removed source was the controller, fall back
    if (sourceToRemove == fControllingSource) {
        LOG_INFO("Removed source was the controller.");
    fControllingSource = nullptr; // Clear controller first

    if (!fClientSources.empty()) {
        fControllingSource = fClientSources.back(); // Fallback to the last one added
        LOG_INFO("Falling back to controlling source");

        // If getting frames, start getting from the new controller
        if (fIsCurrentlyGettingFrames) { // Use new flag
             auto fallbackIt = fSourceContexts.find(fControllingSource);
             if (fallbackIt != fSourceContexts.end()) {
                 LOG_INFO("Sink is active, requesting frames from new fallback controlling source."); // Updated log
                 fControllingSource->getNextFrame(fClientReceiveBuffer, kClientReceiveBufferSize,
                                                  incomingDataHandler, fallbackIt->second,
                                                  sourceClosureHandler, fallbackIt->second);
                 } else {
                     LOG_ERROR("Fallback source has no context! Cannot request frames.");
                     fControllingSource = nullptr; // Cannot proceed
                 }
            }
        } else {
            LOG_INFO("No client sources left after removal.");
            fControllingSource = nullptr;
        }
    }
    // Note: We don't need to explicitly stop non-controlling sources here
    // because handleIncomingData ignores their frames anyway.
}

// Static callback wrapper for incoming frames from clients
void BackchannelSink::incomingDataHandler(void* clientData, unsigned frameSize, // Renamed class
                                                  unsigned numTruncatedBytes,
                                                  struct timeval presentationTime, // Keep time/duration args for now, might be useful later
                                                  unsigned durationInMicroseconds) {
    ClientSourceContext* context = static_cast<ClientSourceContext*>(clientData);
    if (context && context->sink) { // Use sink
        // Pass only frameSize and numTruncatedBytes as time/duration aren't used immediately
        context->sink->handleIncomingData(context, frameSize, numTruncatedBytes);
    } else {
         LOG_ERROR("Invalid context in incomingDataHandler");
    }
}

// Per-instance handler for incoming frames from clients
void BackchannelSink::handleIncomingData(ClientSourceContext* context, unsigned frameSize, unsigned numTruncatedBytes) { // Renamed class, simplified args
    FramedSource* currentSource = context->source;

    // --- Critical Section: Check if this source is the controller ---
    if (currentSource != fControllingSource) {
        // LOG_DEBUG("Ignoring frame from non-controlling source: " << currentSource);
        // Do NOT request the next frame for this non-controlling source
        // However, we MUST re-request from the *controlling* source if it exists and we are playing,
        // otherwise the pipeline might stall if the controller stops sending.
        // This seems complex. Simpler: Only the controller requests next frames.
        return;
    }
    // --- End Critical Section ---

    // If we get here, currentSource == fControllingSource

    if (numTruncatedBytes > 0) {
        LOG_WARN("Controlling source sent truncated frame (" << frameSize << " bytes, " << numTruncatedBytes << " truncated). Discarding.");
        // Continue getting next frame below
    } else if (frameSize == 0) {
         LOG_DEBUG("Controlling source sent zero-sized frame. Ignoring.");
         // Continue getting next frame below
    } else {
        // Valid frame received from the controlling source
        // Push it onto the input queue for the worker thread
        if (fInputQueue) {
            // Create a vector and copy the data
            std::vector<uint8_t> rtpPacket(fClientReceiveBuffer, fClientReceiveBuffer + frameSize);
            // LOG_DEBUG("Pushing RTP packet (size: " << frameSize << ") to backchannel queue.");
            // Use write() instead of try_write(). It returns false if an old element was dropped.
            if (!fInputQueue->write(std::move(rtpPacket))) {
                 // Log if an element was dropped due to buffer being full
                 LOG_WARN("Backchannel input queue was full. Oldest packet dropped.");
            }
        } else {
             LOG_ERROR("Input queue is null, cannot queue packet!");
        }
    }

    // Request the next frame *only* from the controlling source if we should still be getting frames
    if (fIsCurrentlyGettingFrames && fControllingSource == currentSource) { // Use new flag
        fControllingSource->getNextFrame(fClientReceiveBuffer, kClientReceiveBufferSize,
                                         incomingDataHandler, context,
                                         sourceClosureHandler, context);
    }
}

// Static callback for client source closure
void BackchannelSink::sourceClosureHandler(void* clientData) { // Renamed class
     ClientSourceContext* context = static_cast<ClientSourceContext*>(clientData);
     if (context && context->sink && context->source) { // Use sink
         LOG_INFO("Source closure detected");
         // Use scheduler to avoid issues if called during source destruction
         context->sink->envir().taskScheduler().scheduleDelayedTask(0, // Use sink
             (TaskFunc*)[](void* cd) {
                 ClientSourceContext* ctx = static_cast<ClientSourceContext*>(cd);
                 // Check if sink still exists before calling member function
                 if (ctx && ctx->sink) { // Use sink
                    ctx->sink->handleSourceClosure(ctx->source); // Use sink
                 }
             },
             context);
     } else {
         LOG_ERROR("Invalid context in sourceClosureHandler");
     }
}

// Per-instance handler for source closure
void BackchannelSink::handleSourceClosure(FramedSource* source) { // Renamed class
     LOG_DEBUG("Handling source closure");
     // Remove the source, which will handle fallback logic etc.
     removeSource(source);
     // Note: The context associated with this source is deleted within removeSource
}


// --- Methods related to downstream delivery are removed or become NO-OPs ---

// This FramedSource doesn't actually deliver frames downstream in the Live555 pipeline.
// Its purpose is to receive frames from clients and push them to the worker queue.
// The base class might still call this, so provide a minimal implementation.
void BackchannelSink::doGetNextFrame() { // Renamed class
    // LOG_DEBUG("BackchannelSink::doGetNextFrame called (NO-OP)");
    // We don't deliver frames this way.
    // However, this might be interpreted by the framework as "start playing".
    // Let's use this to trigger requests from the *client* sources.
    if (!fIsCurrentlyGettingFrames) {
         fIsCurrentlyGettingFrames = true;
         // If there's a controlling source, start requesting frames from it.
         if (fControllingSource != nullptr) {
             auto it = fSourceContexts.find(fControllingSource);
             if (it != fSourceContexts.end()) {
                 LOG_INFO("doGetNextFrame: Starting frame requests from controller");
                 fControllingSource->getNextFrame(fClientReceiveBuffer, kClientReceiveBufferSize,
                                                  incomingDataHandler, it->second,
                                                  sourceClosureHandler, it->second);
             } else {
                  LOG_ERROR("Controlling source has no context in doGetNextFrame!");
             }
         } else {
              LOG_WARN("doGetNextFrame called, but no controlling source available yet.");
         }
    }
}

// Called when the downstream component (the subsession using this source) stops.
void BackchannelSink::doStopGettingFrames() { // Renamed class
    LOG_INFO("BackchannelSink::doStopGettingFrames called");
    fIsCurrentlyGettingFrames = false;

    // Stop asking the current controlling source for frames
    if (fControllingSource != nullptr) {
        LOG_DEBUG("Stopping frame requests from controller");
        fControllingSource->stopGettingFrames();
    }
    // We don't need to stop non-controlling sources as they aren't being polled.
}

// --- deliverFrame method removed ---
