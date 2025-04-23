#include "JPEGWorker.hpp"

#include "Config.hpp"
#include "Logger.hpp"
#include "WorkerUtils.hpp"
#include "globals.hpp"

#include <cstdio>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define MODULE "JPEGWorker"

JPEGWorker::JPEGWorker(int jpgChnIndex)
    : jpgChn(jpgChnIndex)
{
    LOG_DEBUG("JPEGWorker created for JPEG channel index " << jpgChn);
}

JPEGWorker::~JPEGWorker()
{
    LOG_DEBUG("JPEGWorker destroyed for JPEG channel index " << jpgChn);
}

int JPEGWorker::save_jpeg_stream(int fd, const JPEGFrame &frame)
{
    if (frame.data.empty())
    {
        LOG_ERROR("Attempted to save an empty JPEG frame.");
        return -1;
    }

    ssize_t bytes_written = write(fd, frame.data.data(), frame.data.size());
    if (bytes_written != static_cast<ssize_t>(frame.data.size()))
    {
        LOG_ERROR("Stream write error: " << strerror(errno));
        return -1; // Return error on write failure
    }

    return 0;
}

void JPEGWorker::run()
{
    LOG_DEBUG("Start JPEG processing run loop for index " << jpgChn);

    // Local stats counters
    uint32_t bps{0}; // Bytes per second
    uint32_t fps{0}; // frames per second

    // timestamp for stream stats calculation
    unsigned long long ms{0};

    // Initialize timestamp for stats calculation
    gettimeofday(&global_jpeg[jpgChn]->stream->stats.ts, NULL);
    global_jpeg[jpgChn]->stream->stats.ts.tv_sec -= 10;

    while (global_jpeg[jpgChn]->running)
    {
        JPEGFrame frame = global_jpeg[jpgChn]->msgChannel->wait_read();

        if (frame.data.empty())
        {
            LOG_DEBUG("Received shutdown signal (empty frame) for JPEG channel " << jpgChn);
            break;
        }

        // Process the received frame
        fps++;
        bps += frame.data.size();

        const char *tempPath = "/tmp/snapshot.tmp"; // Temporary path
        const char *finalPath = global_jpeg[jpgChn]
                                    ->stream->jpeg_path; // Final path for the JPEG snapshot

        // Open and create temporary file
        int snap_fd = open(tempPath, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (snap_fd >= 0)
        {
            // Save the JPEG frame data to the file
            if (save_jpeg_stream(snap_fd, frame) == 0)
            {
                // Close the temporary file after writing is done
                close(snap_fd);

                // Atomically move the temporary file to the final destination
                if (rename(tempPath, finalPath) != 0)
                {
                    LOG_ERROR("Failed to move JPEG snapshot from "
                              << tempPath << " to " << finalPath << ": " << strerror(errno));
                    std::remove(tempPath); // Attempt to remove temp file on failure
                }
                // else { LOG_DDEBUG("JPEG snapshot successfully updated"); }
            }
            else
            {
                LOG_ERROR("Failed to save JPEG stream to fd " << snap_fd);
                close(snap_fd);
                std::remove(tempPath);
            }
        }
        else
        {
            LOG_ERROR("Failed to open JPEG snapshot for writing: " << tempPath << ": "
                                                                   << strerror(errno));
        }

        // Update stats periodically
        ms = WorkerUtils::tDiffInMs(&global_jpeg[jpgChn]->stream->stats.ts);
        if (ms > 1000)
        {
            global_jpeg[jpgChn]->stream->stats.fps = fps;
            global_jpeg[jpgChn]->stream->stats.bps = bps;
            fps = 0;
            bps = 0;
            gettimeofday(&global_jpeg[jpgChn]->stream->stats.ts, NULL);

            LOG_DDEBUG("JPG " << jpgChn << " fps: " << global_jpeg[jpgChn]->stream->stats.fps
                              << " bps: " << global_jpeg[jpgChn]->stream->stats.bps
                              << " ms: " << ms);
        }
    }

    LOG_DEBUG("Exiting JPEG processing run loop for index " << jpgChn);
}

void *JPEGWorker::thread_entry(void *arg)
{
    LOG_DEBUG("Start jpeg_grabber thread.");

    StartHelper *sh = static_cast<StartHelper *>(arg);
    // jpgChn is now determined by the index in the global_jpeg array,
    // assuming StartHelper::encChn was previously used to derive this (e.g., encChn 2 -> jpgChn 0)
    // If StartHelper needs adjustment, that's outside this refactor's scope.
    // For now, assume a mapping exists or is handled elsewhere.
    // Let's derive jpgChn based on the old logic for now, needs verification.
    int jpgChn = sh->encChn - 2;
    if (jpgChn < 0 || jpgChn >= NUM_VIDEO_CHANNELS)
    {
        LOG_ERROR("Invalid jpgChn derived from StartHelper encChn: " << sh->encChn);
        sh->has_started.release(); // Signal main even on error
        return nullptr;
    }

    // Set the stream channel based on config (no IMP interaction needed here)
    global_jpeg[jpgChn]->streamChn = global_jpeg[jpgChn]->stream->jpeg_channel;

    // Set stream dimensions based on linked video stream (no IMP interaction)
    if (global_jpeg[jpgChn]->streamChn == 0)
    {
        cfg->stream2.width = cfg->stream0.width;
        cfg->stream2.height = cfg->stream0.height;
    }
    else
    {
        cfg->stream2.width = cfg->stream1.width;
        cfg->stream2.height = cfg->stream1.height;
    }

    // inform main that initialization is complete
    sh->has_started.release();

    global_jpeg[jpgChn]->active = true;
    global_jpeg[jpgChn]->running = true;
    JPEGWorker worker(jpgChn);
    worker.run();


    LOG_DEBUG("JPEGWorker thread finished for channel " << jpgChn);
    return nullptr;
}

void JPEGWorker::activateProducer(int jpgChn, int& first_request_delay_us)
{
    if (jpgChn < 0 || jpgChn >= NUM_VIDEO_CHANNELS || !global_jpeg[jpgChn]) {
        LOG_ERROR("Invalid jpgChn provided to activateProducer: " << jpgChn);
        return;
    }

    int responsible_worker_index = global_jpeg[jpgChn]->streamChn;
    if (responsible_worker_index >= 0 && responsible_worker_index < NUM_VIDEO_CHANNELS && global_video[responsible_worker_index])
    {
        // Check if the responsible VideoWorker is inactive
        if (!global_video[responsible_worker_index]->active)
        {
            LOG_DEBUG("Activating VideoWorker " << responsible_worker_index << " for JPEG channel " << jpgChn);
            first_request_delay_us = cfg->websocket.first_image_delay * 1000; // Set delay if activating

            // Lock mutex before modifying shared state and signaling
            std::unique_lock<std::mutex> lock_stream{mutex_main};
            global_video[responsible_worker_index]->run_for_jpeg = true;
            global_video[responsible_worker_index]->should_grab_frames.notify_one();
            lock_stream.unlock(); // Unlock before waiting

            // Wait for the VideoWorker to signal activation
            global_video[responsible_worker_index]->is_activated.acquire();
            LOG_DEBUG("VideoWorker " << responsible_worker_index << " activated for JPEG channel " << jpgChn);
        } else {
             LOG_DDEBUG("VideoWorker " << responsible_worker_index << " already active for JPEG channel " << jpgChn);
             first_request_delay_us = 0; // No extra delay needed if already active
        }
    } else {
        LOG_ERROR("Invalid streamChn configured for JPEG channel " << jpgChn << ": " << responsible_worker_index);
    }
}

void JPEGWorker::deinit(int jpgChn)
{
    LOG_DEBUG("Sending shutdown signal (empty frame) for JPEG channel " << jpgChn);
    JPEGFrame shutdown_signal;
    global_jpeg[jpgChn]->running = false;
    global_jpeg[jpgChn]->msgChannel->write(shutdown_signal);
}
