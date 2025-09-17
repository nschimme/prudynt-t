#include "InotifyFileWatcher.hpp"

#include "../Logger.hpp"

#include <sys/inotify.h>
#include <unistd.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

void InotifyFileWatcher::watch(const std::string& file_path, void (*callback)()) {
    int inotifyFd = inotify_init();
    if (inotifyFd < 0) {
        LOG_ERROR("inotify_init() failed");
        return;
    }

    int watchDescriptor = inotify_add_watch(inotifyFd,
                                            file_path.c_str(),
                                            IN_MODIFY);
    if (watchDescriptor == -1) {
        LOG_ERROR("inotify_add_watch() failed for " << file_path);
        close(inotifyFd);
        return;
    }

    char buffer[EVENT_BUF_LEN];

    LOG_DEBUG("Monitoring file for changes: " << file_path);

    while (true) {
        int length = read(inotifyFd, buffer, EVENT_BUF_LEN);
        if (length < 0) {
            LOG_ERROR("Error reading file change notification.");
            break;
        }

        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];

            if (event->mask & IN_MODIFY) {
                LOG_INFO("File " << file_path << " changed, triggering callback.");
                callback();
            }

            i += EVENT_SIZE + event->len;
        }
    }

    inotify_rm_watch(inotifyFd, watchDescriptor);
    close(inotifyFd);
}
