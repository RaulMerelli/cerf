#pragma once
#include <string>

struct DeviceMeta;

/* Human-readable "OS" label from a device's cerf.json meta: the os_name with
   "{major}.{minor}" appended only when the name does not already carry that
   version. Mirrors the launcher's table OS column (device_tree.py
   _table_os_label / _os_name_has_version) so host and launcher agree. */
std::string OsDisplayLabel(const DeviceMeta& meta);
