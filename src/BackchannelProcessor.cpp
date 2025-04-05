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
#include "globals.hpp"     // For cfg access

#define MODULE "BackchannelProcessor"

// Helper to map RTP payload type to IMP payload type
IMPAudioPalyloadType BackchannelProcessor::mapRtpToImpPayloadType(uint8_t rtpPayloadType) {
    switch (rtpPayloadType) {
        case 0: return PT_G711U; // PCMU
        case 8: return PT_G711A; // PCMA
        default: return PT_MAX;
    }
}

BackchannelProcessor::BackchannelProcessor(backchannel_stream* stream_data)
    : fStream(stream_data), fPipe(nullptr)
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


// Opens the pipe to the external process
bool BackchannelProcessor::initPipe() {
     if (fPipe) {
         LOG_WARN("Pipe already initialized.");
         return true;
     }
     LOG_INFO("Opening pipe to: /bin/iac -s");
     fPipe = popen("/bin/iac -s", "w");
     if (fPipe == nullptr) {
         LOG_ERROR("popen failed: " << strerror(errno));
         return false;
     }
     LOG_INFO("Pipe opened successfully.");
     return true;
}

// Closes the pipe if it's open
void BackchannelProcessor::closePipe() {
    if (fPipe) {
        LOG_INFO("Closing pipe.");
        int ret = pclose(fPipe);
        fPipe = nullptr;
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
    std::vector<uint8_t> rtpPacket = fStream->inputQueue->wait_read();

    // Check running flag after wake-up
    if (!fStream->running) {
        return false; // Signal to stop main loop
    }

    // Discard any packet read while transitioning from idle,
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
    std::vector<uint8_t> rtpPacket = fStream->inputQueue->wait_read();

    // Check running flag immediately after waking up
    if (!fStream->running) {
        return false; // Signal to stop main loop
    }

    if (rtpPacket.empty()) {
        // Can happen if queue is notified to wake up for shutdown
        return true; // Continue main loop
    }

    // Process the received packet
    if (!processPacket(rtpPacket)) {
        // processPacket returns false on critical errors (like pipe write failure)
        return false; // Signal to stop main loop
    }

    return true; // Continue main loop
}

// Decodes a packet based on RTP payload type
bool BackchannelProcessor::decodePacket(const uint8_t* payload, size_t payloadSize, uint8_t rtpPayloadType, std::vector<int16_t>& outPcmBuffer, int& outSampleRate) {
    IMPAudioPalyloadType impPayloadType = mapRtpToImpPayloadType(rtpPayloadType);
    if (impPayloadType == PT_MAX) {
        LOG_WARN("Unsupported RTP payload type received: " << static_cast<int>(rtpPayloadType));
        return false; // Indicate failure for unsupported type
    }

    // --- G.711 Decoding ---
    if (impPayloadType == PT_G711A || impPayloadType == PT_G711U) {
        int adChn = IMPBackchannel::getADECChannel(impPayloadType);
        if (adChn < 0) {
            LOG_WARN("No ADEC channel ready for payload type: " << static_cast<int>(rtpPayloadType));
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
            outSampleRate = 8000; // G.711 is always 8kHz
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
    // else if (impPayloadType == PT_OPUS) { ... }

    LOG_ERROR("Decoder logic not implemented for payload type: " << static_cast<int>(rtpPayloadType));
    return false; // Indicate failure
}

// Writes PCM data to the pipe
bool BackchannelProcessor::writePcmToPipe(const std::vector<int16_t>& pcmBuffer) {
    if (fPipe == nullptr) {
        LOG_ERROR("Pipe is closed, cannot write PCM data.");
        return false; // Indicate pipe error
    }
    if (pcmBuffer.empty()) {
        LOG_WARN("Attempted to write empty PCM buffer to pipe.");
        return true; // Not an error, just nothing to write
    }

    size_t bytesToWrite = pcmBuffer.size() * sizeof(int16_t);
    size_t bytesWritten = fwrite(pcmBuffer.data(), 1, bytesToWrite, fPipe);

    if (bytesWritten < bytesToWrite) {
        int stream_error = ferror(fPipe);
        int stream_eof = feof(fPipe);
        int saved_errno = errno;
        LOG_ERROR("fwrite to pipe failed (EOF=" << stream_eof << ", ERR=" << stream_error << ", errno=" << saved_errno << ": " << strerror(saved_errno) << "). Assuming pipe closed.");
        closePipe(); // Close the pipe immediately on error
        return false; // Indicate pipe error
    }
    // Optional: fflush(fPipe); // Consider if immediate flushing is needed

    return true; // Success
}


// Processes a single RTP packet: decode, resample, write
bool BackchannelProcessor::processPacket(const std::vector<uint8_t>& rtpPacket) {
    // --- Parse RTP Header ---
    const unsigned RtpHeaderSize = 12;
    if (rtpPacket.size() < RtpHeaderSize) {
        LOG_WARN("Received packet too small for RTP header (" << rtpPacket.size() << " bytes). Discarding.");
        return true; // Continue processing next packet
    }
    uint8_t rtpPayloadType = rtpPacket[1] & 0x7F;
    unsigned headerSize = RtpHeaderSize; // Assuming no extensions/CSRC
    if (rtpPacket.size() <= headerSize) {
        LOG_WARN("Received RTP packet with no payload. Discarding.");
        return true; // Continue processing next packet
    }
    const uint8_t* encodedPayload = rtpPacket.data() + headerSize;
    unsigned encodedPayloadSize = rtpPacket.size() - headerSize;

    // --- Decode ---
    std::vector<int16_t> decoded_pcm;
    int input_rate = 0;
    if (!decodePacket(encodedPayload, encodedPayloadSize, rtpPayloadType, decoded_pcm, input_rate)) {
        // Error already logged in decodePacket
        return true; // Continue processing next packet
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
