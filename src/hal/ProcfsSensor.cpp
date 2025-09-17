#include "ProcfsSensor.hpp"
#include "../Logger.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <fstream>
#include <filesystem>

const std::string ProcfsSensor::SENSOR_PROC_DIR = "/proc/jz/sensor/";

SensorInfo ProcfsSensor::get_info() {
    LOG_DEBUG("Getting sensor information from /proc/jz/sensor/");

    if (!is_available()) {
        throw std::runtime_error("Sensor proc filesystem /proc/jz/sensor/ is not accessible");
    }

    SensorInfo info;

    info.name = read_proc_string("name");
    info.chip_id = read_proc_string("chip_id");
    info.i2c_addr = read_proc_string("i2c_addr");
    info.version = read_proc_string("version");
    info.width = read_proc_int("width", info.width);
    info.height = read_proc_int("height", info.height);
    info.min_fps = read_proc_int("min_fps", info.min_fps);
    info.max_fps = read_proc_int("max_fps", info.max_fps);
    info.i2c_bus = read_proc_int("i2c_bus", info.i2c_bus);
    info.boot = read_proc_int("boot", info.boot);
    info.mclk = read_proc_int("mclk", info.mclk);
    info.video_interface = read_proc_int("video_interface", info.video_interface);
    info.reset_gpio = read_proc_int("reset_gpio", info.reset_gpio);

    if (!info.i2c_addr.empty()) {
        info.i2c_address = parse_hex_string(info.i2c_addr);
    }

    info.fps = info.max_fps;

    LOG_INFO("Successfully retrieved sensor info: " << info.name
                          << " (" << info.width << "x" << info.height
                          << "@" << info.max_fps << "fps)");

    return info;
}

bool ProcfsSensor::is_available() {
    return std::filesystem::exists(SENSOR_PROC_DIR) && std::filesystem::is_directory(SENSOR_PROC_DIR);
}

std::string ProcfsSensor::read_proc_string(const std::string& filename) {
    std::string fullPath = SENSOR_PROC_DIR + filename;
    std::ifstream file(fullPath);

    if (!file.is_open()) {
        LOG_DEBUG("Failed to open " << fullPath);
        return "";
    }

    std::string line;
    if (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        LOG_DEBUG("Read from " << fullPath << ": " << line);
        return line;
    }

    LOG_DEBUG("No content in " << fullPath);
    return "";
}

int ProcfsSensor::read_proc_int(const std::string& filename, int default_value) {
    std::string value = read_proc_string(filename);

    if (value.empty()) {
        LOG_DEBUG("Using default value " << default_value << " for " << filename);
        return default_value;
    }

    try {
        int result = std::stoi(value);
        LOG_DEBUG("Parsed " << filename << " as int: " << result);
        return result;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse '" << value << "' as int from " << filename << ": " << e.what());
        return default_value;
    }
}

unsigned int ProcfsSensor::parse_hex_string(const std::string& hex_str) {
    if (hex_str.empty()) {
        return 0;
    }

    try {
        if (hex_str.substr(0, 2) == "0x" || hex_str.substr(0, 2) == "0X") {
            return static_cast<unsigned int>(std::stoul(hex_str, nullptr, 16));
        } else {
            return static_cast<unsigned int>(std::stoul(hex_str, nullptr, 16));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse hex string '" << hex_str << "': " << e.what());
        return 0;
    }
}
