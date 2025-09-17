#pragma once

#include <string>

class FileWatcher {
public:
    virtual ~FileWatcher() = default;
    virtual void watch(const std::string& file_path, void (*callback)()) = 0;
};
