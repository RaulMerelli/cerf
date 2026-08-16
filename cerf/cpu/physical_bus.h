#pragma once

#include "../core/service.h"

#include <cstdint>

enum class BusWidth : uint32_t { Byte = 1, Half = 2, Word = 4 };

class PhysicalBus : public Service {
public:
    using Service::Service;

    bool Read (uint32_t pa, BusWidth width, uint32_t* out);
    bool Write(uint32_t pa, BusWidth width, uint32_t value);
};
