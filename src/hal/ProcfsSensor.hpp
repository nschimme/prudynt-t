#pragma once

#include "Sensor.hpp"

class ProcfsSensor : public Sensor {
public:
    SensorInfo get_info() override;
    bool is_available() override;

private:
    static const std::string SENSOR_PROC_DIR;
    static std::string read_proc_string(const std::string& filename);
    static int read_proc_int(const std::string& filename, int default_value = 0);
    static unsigned int parse_hex_string(const std::string& hex_str);
};
