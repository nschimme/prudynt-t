#pragma once

#include "Sensor.hpp"

class Video4LinuxSensor : public Sensor {
public:
    SensorInfo get_info() override;
    bool is_available() override;
};
