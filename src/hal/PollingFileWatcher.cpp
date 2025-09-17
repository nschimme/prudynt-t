#include "PollingFileWatcher.hpp"

#include "../Logger.hpp"

#include <sys/stat.h>
#include <thread>
#include <chrono>

void PollingFileWatcher::watch(const std::string& file_path, void (*callback)()) {
    struct stat fileInfo;
    time_t lastModifiedTime = 0;

    while (true) {
        if (stat(file_path.c_str(), &fileInfo) == 0) {
            if (lastModifiedTime == 0) {
                lastModifiedTime = fileInfo.st_mtime;
            } else if (fileInfo.st_mtime != lastModifiedTime) {
                lastModifiedTime = fileInfo.st_mtime;
                LOG_INFO("File " << file_path << " changed, triggering callback.");
                callback();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}
