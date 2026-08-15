#pragma once

#include "service.h"

class Fatal : public Service {
public:
    using Service::Service;

    [[noreturn]] void Die(const char* fmt, ...);
};
