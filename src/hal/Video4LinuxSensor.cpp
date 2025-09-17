#include "Video4LinuxSensor.hpp"
#include "../Logger.hpp"

#include <stdexcept>

SensorInfo Video4LinuxSensor::get_info() {
    LOG_DEBUG("Getting sensor information from video4linux.");
    // In a real implementation, this would query the video device
    // using v4l2 ioctls like VIDIOC_QUERYCAP and VIDIOC_ENUM_FMT.
    throw std::runtime_error("video4linux sensor is not implemented yet.");
}

bool Video4LinuxSensor::is_available() {
    LOG_DEBUG("Checking for video4linux devices.");
    // In a real implementation, this would check for the existence of
    // video devices like /dev/video0.
    return false;
}
