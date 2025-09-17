#include "SystemSensor.hpp"

#if SENSOR_IMPL == SENSOR_IMPL_PROCFS
#include "hal/ProcfsSensor.hpp"
#elif SENSOR_IMPL == SENSOR_IMPL_V4L
#include "hal/Video4LinuxSensor.hpp"
#else
#error "No sensor implementation defined"
#endif

std::unique_ptr<Sensor> SystemSensor::create() {
#if SENSOR_IMPL == SENSOR_IMPL_PROCFS
    return std::make_unique<ProcfsSensor>();
#elif SENSOR_IMPL == SENSOR_IMPL_V4L
    return std::make_unique<Video4LinuxSensor>();
#endif
}
