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
        if (!global_audio[encChn]->msgChannel->write(frame_to_send)) {
            LOG_ERROR("audio channel:" << encChn << " sink clogged!");
        } else {
            std::unique_lock<std::mutex> lock_stream{global_audio[encChn]->onDataCallbackLock};
            if (global_audio[encChn]->onDataCallback)
                global_audio[encChn]->onDataCallback();
        }
    }
}

void AudioWorker::process_raw_frame(AudioFrame& frame) {
    Audio* audio = global_audio[encChn]->audio;
    if (audio->get_output_channel_count() == 2 && frame.soundmode == 1) { // 1 = MONO
        size_t sample_size = frame.bitwidth / 8;
        size_t num_samples = frame.data.size() / sample_size;
        size_t stereo_size = frame.data.size() * 2;

        AudioFrame stereo_frame = frame;
        stereo_frame.data.resize(stereo_size);

        for (size_t i = 0; i < num_samples; i++) {
            uint8_t* mono_sample = frame.data.data() + (i * sample_size);
            uint8_t* stereo_left = stereo_frame.data.data() + (i * sample_size * 2);
            uint8_t* stereo_right = stereo_left + sample_size;
            memcpy(stereo_left, mono_sample, sample_size);
            memcpy(stereo_right, mono_sample, sample_size);
        }

        stereo_frame.soundmode = 2; // 2 = STEREO
        process_frame(stereo_frame);
    } else {
        process_frame(frame);
    }
}

void AudioWorker::run() {
    LOG_DEBUG("Start audio processing run loop for channel " << encChn);
    Audio* audio = global_audio[encChn]->audio;

    if (audio->get_format() == AudioFormat::AAC) {
        reframer = std::make_unique<AudioReframer>(
            audio->get_samplerate(),
            audio->get_samplerate() * 0.040,
            1024);
        LOG_DEBUG("AudioReframer created for channel " << encChn);
    }

    while (global_audio[encChn]->running) {
        if (global_audio[encChn]->hasDataCallback && cfg->audio.input_enabled
            && (global_video[0]->hasDataCallback || global_video[1]->hasDataCallback))
        {
            if (audio->poll_frame(cfg->general.imp_polling_timeout) == 0) {
                AudioFrame frame = audio->get_frame();
                if (!frame.data.empty()) {
                    if (reframer) {
                        reframer->addFrame(frame.data.data(), frame.timestamp.tv_sec * 1000000 + frame.timestamp.tv_usec);
                        while (reframer->hasMoreFrames()) {
                            std::vector<uint8_t> frameData(1024 * sizeof(uint16_t) * audio->get_output_channel_count(), 0);
                            int64_t audio_ts;
                            reframer->getReframedFrame(frameData.data(), audio_ts);

                            AudioFrame reframed;
                            reframed.bitwidth = frame.bitwidth;
                            reframed.soundmode = frame.soundmode;
                            reframed.timestamp.tv_sec = audio_ts / 1000000;
                            reframed.timestamp.tv_usec = audio_ts % 1000000;
                            reframed.data = frameData;

                            process_raw_frame(reframed);
                        }
                    } else {
                        process_raw_frame(frame);
                    }
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
