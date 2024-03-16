#include "Config.hpp"
#include "Scripts.hpp"
#include "Logger.hpp"

int Scripts::motionScript() {
    LOG_DEBUG("Executing motion script.");

    int ret;
    char formatted[512];
    memset(formatted, 0, 512);
    snprintf(
        formatted,
        512,
        Config::singleton()->motionScriptPath.c_str()
    );

    ret = system(formatted);
    if (ret != 0) {
// Assuming LOG_ERROR accepts a C-style string (const char*)
LOG_ERROR(std::string("Motion script failed:") + Config::singleton()->motionScriptPath.c_str());
    }
    return ret;
}
