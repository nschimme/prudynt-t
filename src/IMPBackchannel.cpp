#include "IMPBackchannel.hpp"

#include <liveMedia.hh>
#include <imp/imp_audio.h> 
#include <opus/opus.h>
#include <stdlib.h>
#include <stdio.h>
// #include <mutex> // Mutex removed
#include "Logger.hpp"
#include "Config.hpp"

#define MODULE "IMPBackchannel"

// Thread-local storage for the Opus decoder instance.
// Assumes the SDK calls open/decode/close for a given channel from the same thread.
thread_local OpusDecoder* tl_opusDecoder = nullptr;
// static std::mutex s_opusMutex; // Mutex removed

// Decoder Callbacks matching IMPAudioDecDecoder signature
static int opus_openDecoder(void *pvoidDecoderAttr, void *pDecoder) {
    // pDecoder is ignored because the SDK doesn't reliably pass the correct value.
    // We manage the instance using thread_local storage.
    // std::lock_guard<std::mutex> lock(s_opusMutex); // Lock removed

    if (tl_opusDecoder != nullptr) {
        LOG_WARN("Opus decoder already initialized for this thread. Ignoring subsequent open call.");
        // Even if already initialized, we don't try to write to pDecoder as it caused crashes.
        return 0; // Indicate success
    }

    const int sample_rate = cfg->audio.output_sample_rate;
    const int channels = 1; // Set back to Mono
    int error;
    tl_opusDecoder = opus_decoder_create(sample_rate, channels, &error);
    if (error != OPUS_OK || !tl_opusDecoder) {
        LOG_ERROR("Failed to create Opus mono decoder for this thread: " << opus_strerror(error)); // Update log
        tl_opusDecoder = nullptr; 
        return -1; // Indicate failure
    }
    LOG_DEBUG("Thread-local Opus MONO decoder opened successfully"); // Update log
    return 0; // Indicate success
}

static int opus_decodeFrm(void *pDecoder, unsigned char *pu8Inbuf, int s32InLen,
                          unsigned short *pu16Outbuf, int* ps32OutLen, int *ps32Chns) {
    // Ignore pDecoder, use static instance with mutex protection for access

    // std::lock_guard<std::mutex> lock(s_opusMutex); // Lock removed

    // CRITICAL CHECK: If decoder isn't initialized for this thread yet, return error.
    if (!tl_opusDecoder) { // Check the thread-local instance
         LOG_ERROR("Opus decoder instance is not initialized for this thread in decodeFrm");
         *ps32OutLen = 0; // Ensure output length is 0 on error
         return -1; // Indicate failure
     }

    const int max_frame_size_per_channel = cfg->audio.output_sample_rate * 120 / 1000;
    const int channels = 1; // Expect Mono output from decoder
    // Decode into the output buffer using the thread-local instance.
    int frame_size_samples_per_channel = opus_decode(tl_opusDecoder, pu8Inbuf, s32InLen, (opus_int16 *)pu16Outbuf, max_frame_size_per_channel, 1);

    if (frame_size_samples_per_channel < 0) {
        LOG_ERROR("Thread-local Opus (mono) decode failed: " << opus_strerror(frame_size_samples_per_channel)); // Update log
        *ps32OutLen = 0;
        return -1; // Indicate failure
    }

    // Output length is samples_per_channel * num_channels * bytes_per_sample
    *ps32OutLen = frame_size_samples_per_channel * channels * sizeof(opus_int16);

    return 0; // Indicate success
}

static int opus_closeDecoder(void *pDecoder) {
    // Ignore pDecoder, use thread-local instance
    // std::lock_guard<std::mutex> lock(s_opusMutex); // Lock removed

    if (!tl_opusDecoder) {
        LOG_WARN("opus_closeDecoder called but thread-local decoder instance is already NULL.");
        return 0; // Indicate success
    }

    // Destroy the thread-local instance
    opus_decoder_destroy(tl_opusDecoder);
    tl_opusDecoder = nullptr; 
    LOG_DEBUG("Thread-local Opus decoder closed successfully");
    return 0; // Indicate success
}

// Handle for the registered Opus decoder
static int opusDecoderHandle = -1;

// --- IMPBackchannel Class Implementation ---

IMPBackchannel* IMPBackchannel::createNew() {
    return new IMPBackchannel();
}

IMPBackchannel::IMPBackchannel() {
    // No need to manage static/thread-local decoder here, init handles registration
    // which should trigger the thread-local openDecoder on the correct thread later.
    init();
}

IMPBackchannel::~IMPBackchannel() {
    deinit();
}

int IMPBackchannel::init() {
    LOG_DEBUG("IMPBackchannel::init()");
    int ret = 0;

    // Register Opus decoder only if not already registered
    // This registration itself should be safe even if called multiple times,
    // but the handle management relies on it being called effectively once.
    if (opusDecoderHandle == -1) {
        IMPAudioDecDecoder opusDecoder; // Use the correct struct name from imp_audio.h
        opusDecoder.type = PT_MAX; // Placeholder type for registration
        snprintf(opusDecoder.name, sizeof(opusDecoder.name), "OPUS"); // Name for the decoder
        opusDecoder.openDecoder = opus_openDecoder;
        opusDecoder.decodeFrm = opus_decodeFrm;
        opusDecoder.getFrmInfo = NULL; // Not needed
        opusDecoder.closeDecoder = opus_closeDecoder;

        ret = IMP_ADEC_RegisterDecoder(&opusDecoderHandle, &opusDecoder);
        if (ret != 0) { 
            LOG_ERROR("Failed to register Opus decoder: " << ret);
            opusDecoderHandle = -1; // Ensure handle remains invalid on failure
            // No need to clean up s_opusDecoder here, as openDecoder shouldn't have been called yet.
        } else {
            LOG_DEBUG("Registered Opus decoder with handle: " << opusDecoderHandle);
        }
    } else {
         LOG_DEBUG("Opus decoder already registered with handle: " << opusDecoderHandle);
    }

    // --- Create Decoder Channels ---
    IMPAudioDecChnAttr adec_attr;
    adec_attr.mode = ADEC_MODE_PACK; // Assuming packet mode

    // Create G711U channel
    adec_attr.bufSize = 20; // Default buffer size for G711
    adec_attr.type = PT_G711U;
    int adChn = (int)IMPBackchannelFormat::PCMU;
    ret = IMP_ADEC_CreateChn(adChn, &adec_attr);
    LOG_DEBUG_OR_ERROR(ret, "IMP_ADEC_CreateChn(PCMU, " << adChn << ")");

    // Create G711A channel
    adec_attr.bufSize = 20; // Default buffer size for G711
    adec_attr.type = PT_G711A;
    adChn = (int)IMPBackchannelFormat::PCMA;
    ret = IMP_ADEC_CreateChn(adChn, &adec_attr);
    LOG_DEBUG_OR_ERROR(ret, "IMP_ADEC_CreateChn(PCMA, " << adChn << ")");

    // Create Opus channel if registration was successful
    // This call should trigger opus_openDecoder via the SDK
    if (opusDecoderHandle != -1) {
        adec_attr.type = (IMPAudioPalyloadType)opusDecoderHandle; 
        adChn = (int)IMPBackchannelFormat::OPUS;
        ret = IMP_ADEC_CreateChn(adChn, &adec_attr);
        LOG_DEBUG_OR_ERROR(ret, "IMP_ADEC_CreateChn(OPUS, " << adChn << ")");
        if (ret != 0) { 
            LOG_ERROR("Failed to create Opus decoder channel: " << ret << ". Unregistering decoder.");
            // If channel creation fails, unregister the decoder
            IMP_ADEC_UnRegisterDecoder(&opusDecoderHandle); // This should call opus_closeDecoder
            opusDecoderHandle = -1;
             // Ensure thread-local decoder is cleaned up if closeDecoder wasn't called by SDK
             // This assumes IMP_ADEC_UnRegisterDecoder calls closeDecoder on the same thread
             // where the channel was created/used. If not, this cleanup might not occur correctly.
             if (tl_opusDecoder != nullptr) {
                 LOG_WARN("Cleaning up thread-local Opus decoder due to channel creation failure.");
                 opus_decoder_destroy(tl_opusDecoder);
                 tl_opusDecoder = nullptr;
             }
        } else {
             LOG_DEBUG("Successfully created Opus decoder channel " << adChn);
        }
    }

    return 0; // Assuming success for now, should check 'ret' values properly
}

void IMPBackchannel::deinit() {
    LOG_DEBUG("IMPBackchannel::deinit()");
    int ret;

    // Destroy channels using the macro
    #define DESTROY_ADEC(EnumName, NameString, PayloadType, Frequency, MimeType) \
        { \
            int adChn = (int)IMPBackchannelFormat::EnumName; \
            /* Skip destroying Opus if handle is invalid (init/channel creation failed) */ \
            if (IMPBackchannelFormat::EnumName == IMPBackchannelFormat::OPUS && opusDecoderHandle == -1) { \
                 LOG_DEBUG("Skipping destroy for Opus channel " << adChn << " due to invalid handle."); \
            } else { \
                ret = IMP_ADEC_DestroyChn(adChn); \
                LOG_DEBUG_OR_ERROR(ret, "IMP_ADEC_DestroyChn(" #EnumName ", " << adChn << ")"); \
            } \
        }
    X_FOREACH_BACKCHANNEL_FORMAT(DESTROY_ADEC)
    #undef DESTROY_ADEC

    // Unregister Opus decoder if it was registered
    // This should trigger opus_closeDecoder via the SDK if the handle is valid
    if (opusDecoderHandle != -1) {
        ret = IMP_ADEC_UnRegisterDecoder(&opusDecoderHandle);
        LOG_DEBUG_OR_ERROR(ret, "IMP_ADEC_UnRegisterDecoder(OPUS, " << opusDecoderHandle << ")");
        opusDecoderHandle = -1; // Reset handle
        // Double-check thread-local decoder cleanup, in case closeDecoder wasn't called by SDK
        // Assumes UnRegisterDecoder calls closeDecoder on the correct thread.
        if (tl_opusDecoder != nullptr) {
             LOG_WARN("Thread-local Opus decoder instance may not have been cleaned up by UnRegisterDecoder. Forcing cleanup.");
             opus_decoder_destroy(tl_opusDecoder);
             tl_opusDecoder = nullptr;
        }
    }
}
