#pragma once
#include <string>

#include "string_utils.h"

/* Device directory: "<exe dir>devices\<name>\". */
inline std::string GetDeviceDir(const std::string& device_name) {
    return GetCerfDir() + "devices\\" + device_name + "\\";
}

inline bool IsAbsoluteHostPath(const std::string& path) {
    if (path.size() >= 2 && path[1] == ':') return true;
    return !path.empty() && (path[0] == '\\' || path[0] == '/');
}

inline std::string ResolveDeviceFile(const std::string& device_name,
                                     const std::string& filename) {
    if (IsAbsoluteHostPath(filename)) return filename;
    return GetDeviceDir(device_name) + filename;
}
