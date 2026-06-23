#pragma once

#include "../core/service.h"

#include <string>

/* Composes the host window title from the device meta + CERF version. */
class WindowTitle : public Service {
public:
    using Service::Service;

    /* "{device_name} • {os} • CERF {ver}" from cerf.json meta; empty meta
       fields are dropped, so an absent meta block yields just "CERF {ver}". */
    std::wstring Compose() const;
};
