#pragma once

#include "FileWatcher.hpp"

class PollingFileWatcher : public FileWatcher {
public:
    void watch(const std::string& file_path, void (*callback)()) override;
};
