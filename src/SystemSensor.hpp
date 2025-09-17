#pragma once

#include "hal/Sensor.hpp"
#include <memory>

class SystemSensor {
public:
    static std::unique_ptr<Sensor> create();
};
