#pragma once

#include "SensorInfo.hpp"

class Sensor {
public:
    virtual ~Sensor() = default;
    virtual SensorInfo get_info() = 0;
    virtual bool is_available() = 0;
};
