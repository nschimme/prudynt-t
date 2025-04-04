#include "BackchannelProcessor.hpp"
#include "Logger.hpp"
#include <imp/imp_audio.h> // Needed for IMP ADEC functions
#include <cstring>         // For memcpy, strerror
#include <vector>
#include <sys/wait.h>      // For pclose status checking
#include <cerrno>          // For errno
#include <chrono>          // For wait_read_for timeout

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
        // Consider throwing an exception as the processor cannot function.
    }
}

BackchannelProcessor::~BackchannelProcessor() {
    // Ensure pipe is closed if run() didn't finish cleanly or wasn't called
    closePipe();
}

// Opens the pipe to the external process
bool BackchannelProcessor::initPipe() {
     if (fPipe) {
         LOG_WARN("Pipe already initialized.");
         return true;
     }
     const char* pipeCommand = "/bin/iac -s"; // Make configurable?
     LOG_INFO("BackchannelProcessor: Opening pipe to: " << pipeCommand);
     fPipe = popen(pipeCommand, "w");
     if (fPipe == nullptr) {
         LOG_ERROR("popen(" << pipeCommand << ") failed: " << strerror(errno));
         return false;
     }
     LOG_INFO("BackchannelProcessor: Pipe opened successfully.");
     return true;
}

// Closes the pipe if it's open
void BackchannelProcessor::closePipe() {
    if (fPipe) {
        LOG_INFO("BackchannelProcessor: Closing pipe.");
        int ret = pclose(fPipe);
        fPipe = nullptr; // Mark as closed regardless of pclose result
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


void BackchannelProcessor::run() {
    if (!fStream) {
        LOG_ERROR("Cannot run BackchannelProcessor: stream data is null.");
        return;
    }

    // Initialize the pipe
    if (!initPipe()) {
         LOG_ERROR("Failed to initialize pipe. BackchannelProcessor thread exiting.");
         return; // Cannot proceed
    }

    // Set running flag (though it should be set by caller in main.cpp)
    // Set running flag (though it should be set by caller in main.cpp)
    // fStream->running = true; // Let main.cpp manage this before thread creation
    LOG_INFO("BackchannelProcessor running, initial state check...");

    while (fStream->running) {

        // Atomically check if there are active sessions. Relaxed memory order is sufficient.
        if (fStream->active_sessions.load(std::memory_order_relaxed) == 0) {
            // --- IDLE STATE ---
            if (fPipe) {
                LOG_INFO("No active backchannel sessions, closing pipe.");
                closePipe(); // Close pipe when idle
            }
            // Wait efficiently for activity or shutdown.
            // wait_read() will block until data arrives or queue is potentially woken for shutdown.
            // We still need to check active_sessions after waking up.
            std::vector<uint8_t> rtpPacket = fStream->inputQueue->wait_read(); // Block here
            if (!fStream->running) break; // Check running flag after wake-up
            // Discard any packet read while transitioning from idle, wait for next loop iteration
            // to check active_sessions again before processing.
            continue;
        } else {
            // --- ACTIVE STATE ---
            if (!fPipe) {
                // Need to open the pipe if sessions are active but pipe is closed
                LOG_INFO("Active backchannel session detected, opening pipe.");
                if (!initPipe()) {
                    LOG_ERROR("Failed to open pipe, cannot process backchannel. Waiting...");
                    std::this_thread::sleep_for(std::chrono::seconds(2)); // Wait before retrying
                    continue; // Try again next loop iteration
                }
            }

            // Pipe should be open now, wait for data
            // Use wait_read() - blocks until data or shutdown signal
             std::vector<uint8_t> rtpPacket = fStream->inputQueue->wait_read();

             if (!fStream->running) break; // Check running flag immediately after waking up

            if (rtpPacket.empty()) {
                // Can happen if queue is notified to wake up for shutdown
                continue;
            }

            // --- Parse RTP Header ---
            const unsigned RtpHeaderSize = 12;
            if (rtpPacket.size() < RtpHeaderSize) {
                LOG_WARN("Received packet too small for RTP header (" << rtpPacket.size() << " bytes). Discarding.");
                continue;
            }
            uint8_t rtpPayloadType = rtpPacket[1] & 0x7F;
            unsigned headerSize = RtpHeaderSize; // Assuming no extensions/CSRC
            if (rtpPacket.size() <= headerSize) {
                LOG_WARN("Received RTP packet with no payload. Discarding.");
                continue;
            }
            uint8_t* encodedPayload = rtpPacket.data() + headerSize;
            unsigned encodedPayloadSize = rtpPacket.size() - headerSize;

            // --- Decode ---
            IMPAudioPalyloadType impPayloadType = mapRtpToImpPayloadType(rtpPayloadType);
            if (impPayloadType == PT_MAX) {
                continue; // Silently discard unsupported types
            }

            int adChn = IMPBackchannel::getADECChannel(impPayloadType);
            if (adChn < 0) {
                continue; // Silently discard if channel not ready/found
            }

            int ret;
            IMPAudioStream stream_in;
            stream_in.stream = encodedPayload;
            stream_in.len = static_cast<int>(encodedPayloadSize);

            ret = IMP_ADEC_SendStream(adChn, &stream_in, BLOCK);
            if (ret != 0) {
                LOG_ERROR("IMP_ADEC_SendStream failed for channel " << adChn << ": " << ret);
                continue; // Skip this packet
            }

            IMPAudioStream stream_out;
            std::vector<int16_t> pcm_buffer; // Moved buffer declaration here
            ret = IMP_ADEC_GetStream(adChn, &stream_out, BLOCK);
            if (ret == 0 && stream_out.len > 0 && stream_out.stream != nullptr) {
                size_t num_samples = stream_out.len / sizeof(int16_t);
                if (stream_out.len % sizeof(int16_t) != 0) {
                    LOG_WARN("Decoded stream length (" << stream_out.len << ") not multiple of sizeof(int16_t). Truncating.");
                    num_samples = stream_out.len / sizeof(int16_t);
                }
                pcm_buffer.assign(reinterpret_cast<int16_t*>(stream_out.stream),
                                reinterpret_cast<int16_t*>(stream_out.stream) + num_samples);
                IMP_ADEC_ReleaseStream(adChn, &stream_out); // Release buffer immediately
            } else if (ret != 0) {
                LOG_ERROR("IMP_ADEC_GetStream failed for channel " << adChn << ": " << ret);
                continue; // Skip this packet
            }
            // If ret == 0 but stream_out is empty, pcm_buffer remains empty.

            // --- Write to Pipe ---
            if (!pcm_buffer.empty()) {
                if (fPipe == nullptr) {
                    LOG_ERROR("Pipe is closed, cannot write PCM data. Exiting loop.");
                    break; // Exit loop if pipe closed unexpectedly
                }
                size_t bytesToWrite = pcm_buffer.size() * sizeof(int16_t);
                size_t bytesWritten = fwrite(pcm_buffer.data(), 1, bytesToWrite, fPipe);

                if (bytesWritten < bytesToWrite) {
                    int stream_error = ferror(fPipe);
                    int stream_eof = feof(fPipe);
                    int saved_errno = errno;
                    if (stream_eof || stream_error) {
                    LOG_ERROR("fwrite to pipe failed (EOF=" << stream_eof << ", ERR=" << stream_error << ", errno=" << saved_errno << ": " << strerror(saved_errno) << "). Assuming pipe closed.");
                    closePipe(); // Close the pipe immediately on error
                    // Continue loop to potentially reopen if still active? Or break? Let's break.
                    break; // Exit loop on write error
                } else {
                    LOG_WARN("fwrite wrote partial data (" << bytesWritten << "/" << bytesToWrite << "). Continuing.");
                    }
                }
                // Optional: fflush(fPipe); // Flush after each write? Might impact performance.
            }
        }
    } // end while(fStream->running)

    // --- Cleanup ---
    LOG_INFO("BackchannelProcessor thread stopping.");
    closePipe(); // Close the pipe now that the loop is finished
}
