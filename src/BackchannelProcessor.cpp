#include "BackchannelProcessor.hpp"
#include "Logger.hpp"
#include <imp/imp_audio.h>
#include <cstring>
#include <vector>
#include <sys/wait.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <fcntl.h>
#include <unistd.h>
#include "globals.hpp"

#define MODULE "BackchannelProcessor"

BackchannelProcessor::BackchannelProcessor()
    : fPipe(nullptr), fPipeFd(-1)
{
}

BackchannelProcessor::~BackchannelProcessor() {
    closePipe();
}

static int getFrequency(IMPBackchannelFormat format) {
    #define RETURN_FREQUENCY(EnumName, NameString, PayloadType, Frequency, MimeType) \
        {   \
            if (IMPBackchannelFormat::EnumName == format) \
                return Frequency; \
        }
    X_FOREACH_BACKCHANNEL_FORMAT(RETURN_FREQUENCY)
    #undef RETURN_FREQUENCY
    abort();
}

std::vector<int16_t> BackchannelProcessor::resampleLinear(const std::vector<int16_t>& input_pcm, int input_rate, int output_rate) {
    assert(input_rate != output_rate);

    double ratio = static_cast<double>(output_rate) / input_rate;
    size_t output_size = static_cast<size_t>(std::max(1.0, std::round(static_cast<double>(input_pcm.size()) * ratio)));

    std::vector<int16_t> output_pcm(output_size);
    size_t input_size = input_pcm.size();

    for (size_t i = 0; i < output_size; ++i) {
        double input_pos = static_cast<double>(i) / ratio;
        size_t index1 = static_cast<size_t>(input_pos);

        if (index1 >= input_size) {
            index1 = input_size - 1;
        }

        int16_t sample1 = input_pcm[index1];
        int16_t sample2 = (index1 + 1 < input_size) ? input_pcm[index1 + 1] : sample1;

        double factor = input_pos - static_cast<double>(index1);

        double interpolated_sample = static_cast<double>(sample1) * (1.0 - factor) + static_cast<double>(sample2) * factor;

        if (interpolated_sample > INT16_MAX) interpolated_sample = INT16_MAX;
        if (interpolated_sample < INT16_MIN) interpolated_sample = INT16_MIN;

        output_pcm[i] = static_cast<int16_t>(interpolated_sample);
    }

    return output_pcm;
}


bool BackchannelProcessor::initPipe() {
    if (fPipe) {
        LOG_DEBUG("Pipe already initialized.");
        return true;
    }
    LOG_INFO("Opening pipe to: /bin/iac -s");
    fPipe = popen("/bin/iac -s", "w");
    if (fPipe == nullptr) {
        LOG_ERROR("popen failed: " << strerror(errno));
        fPipeFd = -1;
        return false;
    }

    fPipeFd = fileno(fPipe);
    if (fPipeFd == -1) {
        LOG_ERROR("fileno failed: " << strerror(errno));
        closePipe();
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

    LOG_INFO("Pipe opened successfully (fd=" << fPipeFd << ").");
    return true;
}

void BackchannelProcessor::closePipe() {
    if (fPipe) {
        LOG_INFO("Closing pipe (fd=" << fPipeFd << ").");
        int ret = pclose(fPipe);
        fPipe = nullptr;
        fPipeFd = -1;
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

bool BackchannelProcessor::handleIdleState() {
    if (fPipe) {
        LOG_INFO("Idle: closing pipe.");
        closePipe();
    }
    if (!global_backchannel) return false;
    BackchannelFrame frame = global_backchannel->inputQueue->wait_read();

    if (!global_backchannel->running) {
        return false;
    }

    return true;
}

bool BackchannelProcessor::handleActiveState() {
    if (!fPipe) {
        LOG_INFO("Active session: opening pipe.");
        if (!initPipe()) {
            LOG_ERROR("Failed to open pipe, cannot process backchannel. Retrying...");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            return true;
        }
    }

    if (!global_backchannel) return false;
    BackchannelFrame frame = global_backchannel->inputQueue->wait_read();

    if (!global_backchannel->running) {
        return false;
    }

    if (frame.payload.empty()) {
        return true;
    }

    if (!processFrame(frame)) {
        return false;
    }

    return true;
}

bool BackchannelProcessor::decodeFrame(const uint8_t* payload, size_t payloadSize, IMPBackchannelFormat format, std::vector<int16_t>& outPcmBuffer) {
    IMPAudioStream stream_in;
    stream_in.stream = const_cast<uint8_t*>(payload);
    stream_in.len = static_cast<int>(payloadSize);

    int adChn = (int)format;
    int ret = IMP_ADEC_SendStream(adChn, &stream_in, BLOCK);
    if (ret != 0) {
        LOG_ERROR("IMP_ADEC_SendStream failed for channel " << adChn << ": " << ret);
        return false;
    }

    IMPAudioStream stream_out;
    ret = IMP_ADEC_GetStream(adChn, &stream_out, BLOCK);
    if (ret == 0 && stream_out.len > 0 && stream_out.stream != nullptr) {
        size_t num_samples = stream_out.len / sizeof(int16_t);
        if (stream_out.len % sizeof(int16_t) != 0) {
            LOG_WARN("Decoded stream length (" << stream_out.len << ") not multiple of int16_t size. Truncating.");
        }
        outPcmBuffer.assign(reinterpret_cast<int16_t*>(stream_out.stream),
                            reinterpret_cast<int16_t*>(stream_out.stream) + num_samples);
        IMP_ADEC_ReleaseStream(adChn, &stream_out);
        return true;
    } else if (ret != 0) {
        LOG_ERROR("IMP_ADEC_GetStream failed for channel " << adChn << ": " << ret);
        return false;
    }

    LOG_DEBUG("ADEC_GetStream succeeded but produced no data.");
    outPcmBuffer.clear();
    return true;
}

bool BackchannelProcessor::writePcmToPipe(const std::vector<int16_t>& pcmBuffer) {
    if (fPipeFd == -1 || fPipe == nullptr) {
        LOG_ERROR("Pipe is closed (fd=" << fPipeFd << "), cannot write PCM data.");
        return false;
    }
    if (pcmBuffer.empty()) {
        LOG_DEBUG("Attempted to write empty PCM buffer to pipe.");
        return true;
    }

    size_t bytesToWrite = pcmBuffer.size() * sizeof(int16_t);
    const uint8_t* dataPtr = reinterpret_cast<const uint8_t*>(pcmBuffer.data());

    ssize_t bytesWritten = write(fPipeFd, dataPtr, bytesToWrite);

    if (bytesWritten == static_cast<ssize_t>(bytesToWrite)) {
        // Success
    } else if (bytesWritten >= 0) {
        LOG_WARN("Partial write to pipe (" << bytesWritten << "/" << bytesToWrite << "). Assuming pipe clogged.");
        return true;
    }
    else {
        int saved_errno = errno;
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
            LOG_WARN("Pipe clogged (EAGAIN/EWOULDBLOCK). Discarding PCM chunk.");
            return true;
        } else if (saved_errno == EPIPE) {
            LOG_ERROR("write() failed: Broken pipe (EPIPE). Assuming pipe closed by reader.");
            closePipe();
            return false;
        } else {
            LOG_ERROR("write() failed: errno=" << saved_errno << ": " << strerror(saved_errno) << ". Assuming pipe closed.");
            closePipe();
            return false;
        }
    }

    return true;
}


bool BackchannelProcessor::processFrame(const BackchannelFrame& frame) {
    std::vector<int16_t> decoded_pcm;
    if (!decodeFrame(frame.payload.data(), frame.payload.size(), frame.format, decoded_pcm)) {
        return true;
    }

    int input_rate = getFrequency(frame.format);
    int target_rate = cfg->audio.output_sample_rate;
    const std::vector<int16_t>* buffer_to_write = nullptr;
    std::vector<int16_t> final_pcm;
    if (input_rate == target_rate) {
        buffer_to_write = &decoded_pcm;
    } else {
        final_pcm = BackchannelProcessor::resampleLinear(decoded_pcm, input_rate, target_rate);
        buffer_to_write = &final_pcm;
    }

    if (buffer_to_write != nullptr && !buffer_to_write->empty()) {
        if (!writePcmToPipe(*buffer_to_write)) {
            return false;
        }
    }

    return true;
}


void BackchannelProcessor::run() {
    if (!global_backchannel) {
        LOG_ERROR("Cannot run BackchannelProcessor: global_backchannel is null.");
        return;
    }

    LOG_INFO("Processor thread running...");

    global_backchannel->running = true;
    while (global_backchannel->running) {
        bool continue_loop = true;
        if (global_backchannel->active_sessions.load(std::memory_order_relaxed) == 0) {
            continue_loop = handleIdleState();
        } else {
            continue_loop = handleActiveState();
        }

        if (!continue_loop) {
            break;
        }
    }

    LOG_INFO("Processor thread stopping.");
    closePipe();
}
