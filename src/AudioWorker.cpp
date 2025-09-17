#include "AudioWorker.hpp"

#include "Config.hpp"
#include "Logger.hpp"
#include "WorkerUtils.hpp"
#include "globals.hpp"
#include "hal/Audio.hpp"
#if HAL_DIR == imp
#include "hal/imp/IMPAudioImpl.hpp"
#elif HAL_DIR == v4l
#include "hal/v4l/V4LAudioImpl.hpp"
#endif

#define MODULE "AudioWorker"

#if defined(AUDIO_SUPPORT)

AudioWorker::AudioWorker(int chn) : encChn(chn) {
    LOG_DEBUG("AudioWorker created for channel " << encChn);
}

AudioWorker::~AudioWorker() {
    LOG_DEBUG("AudioWorker destroyed for channel " << encChn);
}

void AudioWorker::process_frame(AudioFrame& frame) {
    Audio* audio = global_audio[encChn]->audio;
    AudioFrame frame_to_send = frame;

    if (audio->supports_encoding()) {
        frame_to_send = audio->encode_frame(frame);
    }

    if (!frame_to_send.data.empty() && global_audio[encChn]->hasDataCallback
        && (global_video[0]->hasDataCallback || global_video[1]->hasDataCallback))
    {
        // The AudioFrame from the HAL now goes into the message channel
        if (!global_audio[encChn]->msgChannel->write(frame_to_send)) {
            LOG_ERROR("audio channel:" << encChn << " sink clogged!");
        } else {
            std::unique_lock<std::mutex> lock_stream{global_audio[encChn]->onDataCallbackLock};
            if (global_audio[encChn]->onDataCallback)
                global_audio[encChn]->onDataCallback();
        }
    }
}

void AudioWorker::run() {
    LOG_DEBUG("Start audio processing run loop for channel " << encChn);
    Audio* audio = global_audio[encChn]->audio;

    // The reframer logic would also need to be adapted to use the generic AudioFrame,
    // but we'll simplify for now.

    while (global_audio[encChn]->running) {
        if (global_audio[encChn]->hasDataCallback && cfg->audio.input_enabled
            && (global_video[0]->hasDataCallback || global_video[1]->hasDataCallback))
        {
            if (audio->poll_frame(cfg->general.imp_polling_timeout) == 0) {
                AudioFrame frame = audio->get_frame();
                if (!frame.data.empty()) {
                    process_frame(frame);
                }
                audio->release_frame(frame);
            }
        } else {
            // Locking and sleeping logic remains the same
            std::unique_lock<std::mutex> lock_stream{mutex_main};
            global_audio[encChn]->active = false;
            while ((global_audio[encChn]->onDataCallback == nullptr
                    || (!global_video[0]->hasDataCallback && !global_video[1]->hasDataCallback))
                   && !global_restart_audio)
            {
                global_audio[encChn]->should_grab_frames.wait(lock_stream);
            }
            global_audio[encChn]->active = true;
        }
    }
}

void *AudioWorker::thread_entry(void *arg) {
    StartHelper *sh = static_cast<StartHelper *>(arg);
    int encChn = sh->encChn;

    LOG_DEBUG("Start audio_grabber thread for device "
              << global_audio[encChn]->devId << " and channel " << global_audio[encChn]->aiChn);

    // Create the concrete implementation and store it in the abstract pointer
#if HAL_DIR == imp
    global_audio[encChn]->audio = new IMPAudioImpl(global_audio[encChn]->devId,
                                                   global_audio[encChn]->aiChn,
                                                   global_audio[encChn]->aeChn);
#elif HAL_DIR == v4l
    global_audio[encChn]->audio = new V4LAudioImpl();
#endif

    if (!global_audio[encChn]->audio->init()) {
        LOG_ERROR("Failed to initialize audio HAL.");
        delete global_audio[encChn]->audio;
        global_audio[encChn]->audio = nullptr;
        sh->has_started.release();
        return nullptr;
    }

    sh->has_started.release();

    global_audio[encChn]->active = true;
    global_audio[encChn]->running = true;

    AudioWorker worker(encChn);
    worker.run();

    if (global_audio[encChn]->audio) {
        global_audio[encChn]->audio->deinit();
        delete global_audio[encChn]->audio;
        global_audio[encChn]->audio = nullptr;
    }

    return 0;
}

#endif // AUDIO_SUPPORT
