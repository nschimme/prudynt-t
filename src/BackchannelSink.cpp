#include "BackchannelSink.hpp"
#include <GroupsockHelper.hh> // For closeSocket
#include <vector>

#define MODULE "BackchannelSink"

BackchannelSink* BackchannelSink::createNew(UsageEnvironment& env, backchannel_stream* stream_data) {
    return new BackchannelSink(env, stream_data);
}

BackchannelSink::BackchannelSink(UsageEnvironment& env, backchannel_stream* stream_data)
    : FramedSource(env),
      fActiveTalker(nullptr), // Initialize active talker to null
      fSilenceTimeoutTask(nullptr), // Initialize timeout task handle
      fSilenceTimeoutMs(500), // Default silence timeout (e.g., 500ms) - TODO: Make configurable?
      fStream(stream_data),
      fInputQueue(nullptr),
      fIsCurrentlyGettingFrames(false)
{
    if (fStream == nullptr) {
         LOG_ERROR("backchannel_stream provided to BackchannelSink is null!");
         // Handle error
    } else {
        fInputQueue = fStream->inputQueue.get();
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
    // Cancel any pending silence timeout task
    envir().taskScheduler().unscheduleDelayedTask(fSilenceTimeoutTask);
    fSilenceTimeoutTask = nullptr;

    // Clean up contexts and stop sources
    // Note: removeSource stops getting frames and handles fActiveTalker/timeout cancellation
    while (!fClientSources.empty()) {
        FramedSource* source = fClientSources.front();
        removeSource(source); // Use removeSource for consistent cleanup
    }
    fSourceContexts.clear(); // Should be empty now

    delete[] fClientReceiveBuffer;
    // fInputQueue is managed externally (via fStream)
}

void BackchannelSink::addSource(FramedSource* newSource) {
    if (newSource == nullptr) return;

    LOG_INFO("Adding client source");

    // Add to the list
    fClientSources.push_back(newSource);

    // Create and store context
    ClientSourceContext* context = new ClientSourceContext{this, newSource};
    fSourceContexts[newSource] = context;

    // Increment active session count using fStream (atomically)
    if (fStream) {
        int previous_count = fStream->active_sessions.fetch_add(1, std::memory_order_relaxed);
        LOG_INFO("Backchannel session added. Active sessions: " << previous_count + 1);
    } else {
        LOG_ERROR("fStream is null in addSource! Cannot increment session count.");
    }

    // If we are actively processing, start requesting frames from this new source immediately.
    // If not currently active (fIsCurrentlyGettingFrames is false), doGetNextFrame will handle starting requests later.
    if (fIsCurrentlyGettingFrames) {
        LOG_INFO("Sink is active, requesting frames from new source.");
        newSource->getNextFrame(fClientReceiveBuffer, kClientReceiveBufferSize,
                                incomingDataHandler, context,
                                sourceClosureHandler, context);
    }
}

void BackchannelSink::removeSource(FramedSource* sourceToRemove) {
    if (sourceToRemove == nullptr) return;

    LOG_INFO("Removing client source");

    // Find and remove from the list
    fClientSources.remove(sourceToRemove);

    // Find and delete the context
    auto it = fSourceContexts.find(sourceToRemove);
    if (it != fSourceContexts.end()) {
        // Stop getting frames *before* deleting context
        sourceToRemove->stopGettingFrames();

        // Decrement active session count BEFORE deleting context (atomically)
        if (fStream) {
            int previous_count = fStream->active_sessions.fetch_sub(1, std::memory_order_relaxed);
            LOG_INFO("Backchannel session removed. Active sessions: " << previous_count - 1);
        } else {
             LOG_ERROR("fStream is null in removeSource! Cannot decrement session count.");
        }

        delete it->second; // Now delete context
        fSourceContexts.erase(it);
    } else {
        LOG_WARN("Attempted to remove source not in context map");
        // Still try to stop it if it might be active
        sourceToRemove->stopGettingFrames();
    }

    // If the removed source was the active talker, clear the active talker and cancel timeout
    if (sourceToRemove == fActiveTalker) {
        LOG_INFO("Removed source was the active talker. Clearing active talker.");
        fActiveTalker = nullptr;
        envir().taskScheduler().unscheduleDelayedTask(fSilenceTimeoutTask);
        fSilenceTimeoutTask = nullptr;
    }
}

// Static callback wrapper for incoming frames from clients
void BackchannelSink::incomingDataHandler(void* clientData, unsigned frameSize,
                                                  unsigned numTruncatedBytes,
                                                  struct timeval presentationTime,
                                                  unsigned durationInMicroseconds) {
    ClientSourceContext* context = static_cast<ClientSourceContext*>(clientData);
    if (context && context->sink) {
        context->sink->handleIncomingData(context, frameSize, numTruncatedBytes);
    } else {
         LOG_ERROR("Invalid context in incomingDataHandler");
    }
}

// Per-instance handler for incoming frames from clients - "First Talker Wins" logic
void BackchannelSink::handleIncomingData(ClientSourceContext* context, unsigned frameSize, unsigned numTruncatedBytes) {
    FramedSource* currentSource = context->source;
    bool processThisPacket = false;

    // --- First Talker Wins Logic ---
    if (fActiveTalker == nullptr) {
        // Line is free, this source becomes the active talker
        fActiveTalker = currentSource;
        LOG_DEBUG("New active talker");
        processThisPacket = true;
    } else if (fActiveTalker == currentSource) {
        // The current active talker continues
        processThisPacket = true;
    } else {
        // Another source tried to talk while the line was busy
        // LOG_DEBUG("Ignoring frame from non-active source: " << currentSource);
        processThisPacket = false;
    }

    // If this source is allowed to talk (or just became the talker)
    if (processThisPacket) {
        // Reset/schedule the silence timeout
        envir().taskScheduler().unscheduleDelayedTask(fSilenceTimeoutTask);
        fSilenceTimeoutTask = envir().taskScheduler().scheduleDelayedTask(
            fSilenceTimeoutMs * 1000, // Convert ms to microseconds
            (TaskFunc*)silenceTimeoutHandler, this);

        // Process the actual packet data
        if (numTruncatedBytes > 0) {
            LOG_WARN("Active talker sent truncated frame (" << frameSize << " bytes, " << numTruncatedBytes << " truncated). Discarding.");
        } else if (frameSize == 0) {
             LOG_DEBUG("Active talker sent zero-sized frame. Ignoring.");
        } else {
            // Valid frame received from the active talker
            if (fInputQueue) {
                std::vector<uint8_t> rtpPacket(fClientReceiveBuffer, fClientReceiveBuffer + frameSize);
                if (!fInputQueue->write(std::move(rtpPacket))) {
                     LOG_WARN("Backchannel input queue was full. Oldest packet dropped.");
                }
            } else {
                 LOG_ERROR("Input queue is null, cannot queue packet!");
            }
        }
    }

    // Always request the next frame from the source that just delivered,
    // regardless of whether it was the active talker or not.
    // This ensures we keep listening to all connected clients.
    if (fIsCurrentlyGettingFrames) {
        currentSource->getNextFrame(fClientReceiveBuffer, kClientReceiveBufferSize,
                                    incomingDataHandler, context,
                                    sourceClosureHandler, context);
    }
}

// Static callback for silence timeout
void BackchannelSink::silenceTimeoutHandler(void* clientData) {
    BackchannelSink* sink = static_cast<BackchannelSink*>(clientData);
    if (sink) {
        LOG_DEBUG("Silence timeout reached. Clearing active talker.");
        sink->fActiveTalker = nullptr;
        sink->fSilenceTimeoutTask = nullptr; // Mark task as complete
    }
}


// Static callback for client source closure
void BackchannelSink::sourceClosureHandler(void* clientData) {
     ClientSourceContext* context = static_cast<ClientSourceContext*>(clientData);
     if (context && context->sink && context->source) {
         LOG_INFO("Source closure detected");
         // Use scheduler to avoid issues if called during source destruction
         context->sink->envir().taskScheduler().scheduleDelayedTask(0,
             (TaskFunc*)[](void* cd) {
                 ClientSourceContext* ctx = static_cast<ClientSourceContext*>(cd);
                 if (ctx && ctx->sink) {
                    ctx->sink->handleSourceClosure(ctx->source);
                 }
             },
             context);
     } else {
         LOG_ERROR("Invalid context in sourceClosureHandler");
     }
}

// Per-instance handler for source closure
void BackchannelSink::handleSourceClosure(FramedSource* source) {
     LOG_DEBUG("Handling source closure");
     // Remove the source, which handles list removal, context deletion,
     // and clearing fActiveTalker/timeout if necessary.
     removeSource(source);
}


// --- Methods related to downstream delivery (FramedSource overrides) ---

// Called by Live555 when the downstream component (subsession) wants data.
// We use this to signal that we should start requesting frames from clients.
void BackchannelSink::doGetNextFrame() {
    if (!fIsCurrentlyGettingFrames) {
         LOG_INFO("doGetNextFrame: Starting frame requests from all client sources.");
         fIsCurrentlyGettingFrames = true;
         // Request frames from all currently connected sources
         for (FramedSource* source : fClientSources) {
             auto it = fSourceContexts.find(source);
             if (it != fSourceContexts.end()) {
                 source->getNextFrame(fClientReceiveBuffer, kClientReceiveBufferSize,
                                      incomingDataHandler, it->second,
                                      sourceClosureHandler, it->second);
             } else {
                  LOG_ERROR("Source has no context in doGetNextFrame!");
             }
         }
    }
    // Note: We don't actually deliver frames *downstream* via deliverFrameData,
    // as this sink's purpose is to *receive* and queue data for the processor thread.
}

// Called when the downstream component stops asking for frames.
void BackchannelSink::doStopGettingFrames() {
    LOG_INFO("doStopGettingFrames called");
    if (fIsCurrentlyGettingFrames) {
        fIsCurrentlyGettingFrames = false;
        // Stop asking all client sources for frames
        LOG_DEBUG("Stopping frame requests from all client sources.");
        for (FramedSource* source : fClientSources) {
            source->stopGettingFrames();
        }
        // Also clear the active talker and cancel timeout as we are stopping
        if (fActiveTalker) {
            LOG_DEBUG("Clearing active talker due to stopGettingFrames.");
            fActiveTalker = nullptr;
            envir().taskScheduler().unscheduleDelayedTask(fSilenceTimeoutTask);
            fSilenceTimeoutTask = nullptr;
        }
    }
}
