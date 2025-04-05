#include "BackchannelProcessor.hpp"
#include "Logger.hpp"
#include <imp/imp_audio.h> // Needed for IMP ADEC functions
#include <cstring>         // For memcpy, strerror
#include <vector>
#include <sys/wait.h>      // For pclose status checking
#include <cerrno>          // For errno
#include <chrono>          // For wait_read_for timeout
#include <cmath>           // For std::round
#include <cstdint>         // For INT16_MAX, INT16_MIN
#include <cassert>         // For assert
#include <fcntl.h>         // For fcntl, O_NONBLOCK
#include <unistd.h>        // For write(), fileno()
#include "globals.hpp"     // For cfg access

#define MODULE "BackchannelProcessor"

// Helper to map IMPBackchannelFormat enum to IMP payload type
IMPAudioPalyloadType BackchannelProcessor::mapFormatToImpPayloadType(IMPBackchannelFormat format) {
    switch (format) {
        case IMPBackchannelFormat::PCMU: return PT_G711U;
        case IMPBackchannelFormat::PCMA: return PT_G711A;
        default: return PT_MAX;
    }
}

BackchannelProcessor::BackchannelProcessor(backchannel_stream* stream_data)
    : fStream(stream_data), fPipe(nullptr), fPipeFd(-1), fLastPipeFullLogTime(std::chrono::steady_clock::time_point::min())
{
    if (fStream == nullptr) {
        LOG_ERROR("backchannel_stream data provided to BackchannelProcessor is null!");
        assert(false);
    }
}

BackchannelProcessor::~BackchannelProcessor() {
    closePipe();
}

// Simple linear interpolation resampling function
// Takes input PCM at input_rate and returns new vector with PCM at output_rate
std::vector<int16_t> BackchannelProcessor::resampleLinear(const std::vector<int16_t>& input_pcm, int input_rate, int output_rate) {
    // Basic validation
    if (input_rate <= 0 || output_rate <= 0 || input_pcm.empty()) {
        LOG_ERROR("Invalid input for resampling: input_rate=" << input_rate << ", output_rate=" << output_rate << ", input_empty=" << input_pcm.empty());
        return {}; // Return empty vector for invalid input
    }

    assert(input_rate != output_rate);

    double ratio = static_cast<double>(output_rate) / input_rate;
    // Use std::round for potentially better size estimate, ensure non-zero
    size_t output_size = static_cast<size_t>(std::max(1.0, std::round(static_cast<double>(input_pcm.size()) * ratio)));

    std::vector<int16_t> output_pcm(output_size);
    size_t input_size = input_pcm.size();

    for (size_t i = 0; i < output_size; ++i) {
        // Calculate the corresponding position in the input signal
        double input_pos = static_cast<double>(i) / ratio;
        size_t index1 = static_cast<size_t>(input_pos); // Floor is implicit

        // Ensure index1 is valid
        if (index1 >= input_size) {
            index1 = input_size - 1; // Clamp to last valid index
        }

        // Get the two nearest input samples
        int16_t sample1 = input_pcm[index1];
        int16_t sample2 = (index1 + 1 < input_size) ? input_pcm[index1 + 1] : sample1; // Duplicate last sample if needed

        // Calculate the interpolation factor
        double factor = input_pos - static_cast<double>(index1);

        // Perform linear interpolation using doubles for intermediate calculation
        double interpolated_sample = static_cast<double>(sample1) * (1.0 - factor) + static_cast<double>(sample2) * factor;

        // Clamp to int16_t range using constants before casting
        if (interpolated_sample > INT16_MAX) interpolated_sample = INT16_MAX;
        if (interpolated_sample < INT16_MIN) interpolated_sample = INT16_MIN;

        output_pcm[i] = static_cast<int16_t>(interpolated_sample);
    }

    return output_pcm;
}


// Opens the pipe to the external process and sets it to non-blocking
bool BackchannelProcessor::initPipe() {
    if (fPipe) {
        LOG_WARN("Pipe already initialized.");
        return true;
    }
    LOG_INFO("Opening pipe to: /bin/iac -s");
    fPipe = popen("/bin/iac -s", "w");
    if (fPipe == nullptr) {
        LOG_ERROR("popen failed: " << strerror(errno));
        fPipeFd = -1;
        return false;
    }

    // Get file descriptor and set to non-blocking
    fPipeFd = fileno(fPipe);
    if (fPipeFd == -1) {
        LOG_ERROR("fileno failed: " << strerror(errno));
        closePipe(); // Close the popen stream if fileno failed
        return false;
    }

    int flags = fcntl(fPipeFd, F_GETFL, 0);
    if (flags == -1) {
        LOG_ERROR("fcntl(F_GETFL) failed: " << strerror(errno));
        closePipe();
        return false;
    }

    if (fcntl(fPipeFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERROR("fcntl(F_SETFL, O_NONBLOCK) failed: " << strerror(errno));
        closePipe();
        return false;
    }

    LOG_INFO("Pipe opened successfully and set to non-blocking (fd=" << fPipeFd << ").");
    return true;
}

// Closes the pipe if it's open
void BackchannelProcessor::closePipe() {
    if (fPipe) {
        LOG_INFO("Closing pipe (fd=" << fPipeFd << ").");
        int ret = pclose(fPipe);
        fPipe = nullptr;
        fPipeFd = -1; // Reset file descriptor
        if (ret == -1) {
            LOG_ERROR("pclose() failed: " << strerror(errno));
        } else {
             if (WIFEXITED(ret)) {
                 LOG_INFO("Pipe process exited with status: " << WEXITSTATUS(ret));
             } else if (WIFSIGNALED(ret)) {
                 LOG_WARN("Pipe process terminated by signal: " << WTERMSIG(ret));
             } else {
                  LOG_WARN("Pipe process stopped for unknown reason.");
             }
        }
    }
}

// Handles logic when processor is idle (no active sessions)
bool BackchannelProcessor::handleIdleState() {
    if (fPipe) {
        LOG_INFO("No active backchannel sessions, closing pipe.");
        closePipe();
    }
    // Wait efficiently for activity or shutdown.
    // Read BackchannelFrame instead of raw vector
    BackchannelFrame frame = fStream->inputQueue->wait_read();

    // Check running flag after wake-up
    if (!fStream->running) {
        return false; // Signal to stop main loop
    }

    // Discard any frame read while transitioning from idle,
    // loop will re-check active_sessions.
    return true; // Continue main loop
}

// Handles logic when processor is active (sessions > 0)
bool BackchannelProcessor::handleActiveState() {
    // Ensure pipe is open
    if (!fPipe) {
        LOG_INFO("Active backchannel session detected, opening pipe.");
        if (!initPipe()) {
            LOG_ERROR("Failed to open pipe, cannot process backchannel. Retrying...");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            return true; // Continue main loop, will retry opening pipe
        }
    }

    // Pipe should be open now, wait for data
    // Read BackchannelFrame instead of raw vector
    BackchannelFrame frame = fStream->inputQueue->wait_read();

    // Check running flag immediately after waking up
    if (!fStream->running) {
        return false; // Signal to stop main loop
    }

    if (frame.payload.empty()) { // Check if payload is empty
        // Can happen if queue is notified to wake up for shutdown
        return true; // Continue main loop
    }

    // Process the received frame
    if (!processFrame(frame)) { // Pass the frame struct
        // processFrame returns false on critical errors (like pipe write failure)
        return false; // Signal to stop main loop
    }

    return true; // Continue main loop
}

// Decodes a frame based on its format
bool BackchannelProcessor::decodeFrame(const uint8_t* payload, size_t payloadSize, IMPBackchannelFormat format, std::vector<int16_t>& outPcmBuffer, int& outSampleRate) {
    IMPAudioPalyloadType impPayloadType = mapFormatToImpPayloadType(format);
    if (impPayloadType == PT_MAX) {
        LOG_WARN("Unsupported BackchannelFormat received: " << static_cast<int>(format));
        return false; // Indicate failure for unsupported type
    }

    // --- G.711 Decoding ---
    if (impPayloadType == PT_G711A || impPayloadType == PT_G711U) {
        // Use the format enum directly with getADECChannel
        int adChn = IMPBackchannel::getADECChannel(format);
        if (adChn < 0) {
            LOG_WARN("No ADEC channel ready for format: " << static_cast<int>(format));
            return false; // Indicate failure
        }

        IMPAudioStream stream_in;
        stream_in.stream = const_cast<uint8_t*>(payload); // IMP API might not be const-correct
        stream_in.len = static_cast<int>(payloadSize);

        int ret = IMP_ADEC_SendStream(adChn, &stream_in, BLOCK);
        if (ret != 0) {
            LOG_ERROR("IMP_ADEC_SendStream failed for channel " << adChn << ": " << ret);
            return false; // Indicate failure
        }

        IMPAudioStream stream_out;
        ret = IMP_ADEC_GetStream(adChn, &stream_out, BLOCK);
        if (ret == 0 && stream_out.len > 0 && stream_out.stream != nullptr) {
            size_t num_samples = stream_out.len / sizeof(int16_t);
            if (stream_out.len % sizeof(int16_t) != 0) {
                LOG_WARN("Decoded stream length (" << stream_out.len << ") not multiple of sizeof(int16_t). Truncating.");
            }
            outPcmBuffer.assign(reinterpret_cast<int16_t*>(stream_out.stream),
                              reinterpret_cast<int16_t*>(stream_out.stream) + num_samples);
            IMP_ADEC_ReleaseStream(adChn, &stream_out);
            // Look up frequency using the helper function
            outSampleRate = IMPBackchannel::getFrequency(format);
            if (outSampleRate == 0) {
                 LOG_ERROR("Failed to get frequency for format: " << static_cast<int>(format));
                 return false; // Indicate failure if frequency is unknown
            }
            return true;
        } else if (ret != 0) {
            LOG_ERROR("IMP_ADEC_GetStream failed for channel " << adChn << ": " << ret);
            return false;
        } else {
            LOG_WARN("Success, but no data produced");
            outPcmBuffer.clear();
            outSampleRate = 0;
            return true;
        }
    }

    // --- Add other decoders here (e.g., Opus) ---
    // else if (format == IMPBackchannelFormat::OPUS) { ... }

    LOG_ERROR("Decoder logic not implemented for format: " << static_cast<int>(format));
    return false; // Indicate failure
}

// Writes PCM data to the non-blocking pipe
bool BackchannelProcessor::writePcmToPipe(const std::vector<int16_t>& pcmBuffer) {
    if (fPipeFd == -1 || fPipe == nullptr) { // Check fd as well
        LOG_ERROR("Pipe is closed (fd=" << fPipeFd << "), cannot write PCM data.");
        return false; // Indicate pipe error
    }
    if (pcmBuffer.empty()) {
        LOG_WARN("Attempted to write empty PCM buffer to pipe.");
        return true; // Not an error, just nothing to write
    }

    size_t bytesToWrite = pcmBuffer.size() * sizeof(int16_t);
    const uint8_t* dataPtr = reinterpret_cast<const uint8_t*>(pcmBuffer.data());
    // size_t totalBytesWritten = 0; // Removed as it's unused with single write attempt

    // Loop in case write returns partial success, though less likely with pipes
    // In non-blocking mode, write usually writes all it can or fails with EAGAIN.
    // A simple single write attempt is often sufficient.
    ssize_t bytesWritten = write(fPipeFd, dataPtr, bytesToWrite);

    if (bytesWritten == static_cast<ssize_t>(bytesToWrite)) {
        // Success
    } else if (bytesWritten >= 0) {
        // Partial write - unusual for pipe, treat as error/clogged for simplicity?
        // Or attempt to write remaining? For now, log and treat as clogged.
        LOG_WARN("Partial write to pipe (" << bytesWritten << "/" << bytesToWrite << "). Assuming pipe clogged.");
        // Consider if partial data should be discarded or retried. Discarding for now.
        return true; // Indicate non-fatal, continue processing
    }
    else { // bytesWritten == -1
        int saved_errno = errno;
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
            // Pipe buffer is full, discard data and continue processing
            auto now = std::chrono::steady_clock::now();
            if (now - fLastPipeFullLogTime > std::chrono::seconds(5)) { // Rate-limit logging
                LOG_WARN("Backchannel pipe clogged (EAGAIN/EWOULDBLOCK). Discarding PCM chunk."); // Use "clogged" terminology
                fLastPipeFullLogTime = now;
            }
            return true; // Indicate success (non-fatal, continue processing)
        } else if (saved_errno == EPIPE) {
            // Pipe closed by reader
            LOG_ERROR("write() failed: Broken pipe (EPIPE). Assuming pipe closed by reader.");
            closePipe();
            return false; // Indicate pipe error
        } else {
            // Other write error
            LOG_ERROR("write() failed: errno=" << saved_errno << ": " << strerror(saved_errno) << ". Assuming pipe closed.");
            closePipe();
            return false; // Indicate pipe error
        }
    }

    // Optional: Consider if fsync(fPipeFd) is needed, though usually not for pipes.
    return true; // Success
}


// Processes a single BackchannelFrame: decode, resample, write
bool BackchannelProcessor::processFrame(const BackchannelFrame& frame) {
    // --- Decode ---
    std::vector<int16_t> decoded_pcm;
    int input_rate = 0; // This will be set by decodeFrame based on format lookup
    if (!decodeFrame(frame.payload.data(), frame.payload.size(), frame.format, decoded_pcm, input_rate)) {
        // Error already logged in decodeFrame
        return true; // Continue processing next frame
    }

    // --- Resample if necessary ---
    const std::vector<int16_t>* buffer_to_write = nullptr;
    std::vector<int16_t> final_pcm; // Holds resampled data if needed

    if (input_rate > 0) { // Check if decode was successful and produced data
        int target_rate = cfg->audio.output_sample_rate; // Get target rate from config

        if (input_rate == target_rate) {
            buffer_to_write = &decoded_pcm; // Pass-through
        } else {
            final_pcm = BackchannelProcessor::resampleLinear(decoded_pcm, input_rate, target_rate);
            buffer_to_write = &final_pcm;
        }
    } else if (!decoded_pcm.empty()) {
         // Should not happen if decodePacket sets input_rate correctly on success
         LOG_WARN("Decoded PCM buffer is not empty, but input sample rate is zero. Cannot resample or write.");
    }
    // If input_rate is 0 and decoded_pcm is empty, decode failed or produced nothing, skip writing.

    // --- Write to Pipe ---
    if (buffer_to_write != nullptr && !buffer_to_write->empty()) {
        if (!writePcmToPipe(*buffer_to_write)) {
            return false; // Pipe error occurred, signal to stop main loop
        }
    }

    return true; // Continue processing next packet
}


// Main processing loop
void BackchannelProcessor::run() {
    if (!fStream) {
        LOG_ERROR("Cannot run BackchannelProcessor: stream data is null.");
        return;
    }

    LOG_INFO("BackchannelProcessor running...");

    while (fStream->running) {
        bool continue_loop = true;
        // Atomically check if there are active sessions.
        if (fStream->active_sessions.load(std::memory_order_relaxed) == 0) {
            continue_loop = handleIdleState();
        } else {
            continue_loop = handleActiveState();
        }

        if (!continue_loop) {
            break; // Exit loop if signaled by state handlers
        }
    }

    LOG_INFO("BackchannelProcessor thread stopping.");
    closePipe();
}
