#pragma once

#include "../core/service.h"

#include <cstdint>

class KeyboardInput : public Service {
public:
    using Service::Service;
    ~KeyboardInput() override = default;

    virtual void OnHostKey(uint8_t vk, bool key_up) = 0;
};
