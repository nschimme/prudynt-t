#include "ConfigWatcher.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "globals.hpp"

#include "hal/FileWatcher.hpp"

#ifdef __linux__
#include "hal/InotifyFileWatcher.hpp"
#else
#include "hal/PollingFileWatcher.hpp"
#endif

#define MODULE "ConfigWatcher"

static void on_config_change() {
    cfg->load();
    LOG_INFO("Config file changed, the config is reloaded from: " << cfg->filePath);
}

ConfigWatcher::ConfigWatcher() {
    LOG_DEBUG("ConfigWatcher created.");
}

ConfigWatcher::~ConfigWatcher() {
    LOG_DEBUG("ConfigWatcher destroyed.");
}

void ConfigWatcher::run() {
    std::unique_ptr<FileWatcher> file_watcher;

#ifdef __linux__
    file_watcher = std::make_unique<InotifyFileWatcher>();
#else
    file_watcher = std::make_unique<PollingFileWatcher>();
#endif

    file_watcher->watch(cfg->filePath, &on_config_change);
}

void* ConfigWatcher::thread_entry(void* arg) {
    (void)arg; // Mark unused
    LOG_DEBUG("Starting config watch thread.");
    ConfigWatcher watcher;
    watcher.run();
    LOG_DEBUG("Exiting config watch thread.");
    return nullptr;
}
