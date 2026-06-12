#pragma once
#include <string>

#include "string_utils.h"

/* Device directory: "<exe dir>devices\<name>\". */
inline std::string GetDeviceDir(const std::string& device_name) {
    return GetCerfDir() + "devices\\" + device_name + "\\";
}
