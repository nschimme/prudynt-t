#pragma once

#include <string>

struct SensorInfo {
    std::string name;           // Sensor model name (e.g., "gc2083")
    std::string chip_id;        // Chip ID (e.g., "0x2083")
    std::string i2c_addr;       // I2C address (e.g., "0x37")
    int width;                  // Native width (e.g., 1920)
    int height;                 // Native height (e.g., 1080)
    int min_fps;                // Minimum FPS (e.g., 5)
    int max_fps;                // Maximum FPS (e.g., 30)
    std::string version;        // Sensor version (e.g., "H20220228a")

    // Additional proc fields for compatibility
    int i2c_bus;                // I2C bus number
    int boot;                   // Boot parameter
    int mclk;                   // MCLK setting
    int video_interface;        // Video interface type
    int reset_gpio;             // Reset GPIO pin

    // Derived values for compatibility
    unsigned int i2c_address;   // Parsed I2C address as uint
    int fps;                    // Default FPS (use max_fps)

    // Constructor with defaults
    SensorInfo() : width(1920), height(1080), min_fps(5), max_fps(30),
                  i2c_bus(0), boot(0), mclk(1), video_interface(0), reset_gpio(-1),
                  i2c_address(0x37), fps(25) {}
};
