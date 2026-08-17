#pragma once

#include "../../core/service.h"

#include <cstdint>

class SdCard;

class Imx6UsdhcAdma final : public Service {
public:
    using Service::Service;

    struct Transfer {
        uint32_t descriptor_base;
        uint32_t block_size;
        uint32_t block_count;
        bool count_limited;
    };

    void Read(SdCard& card, const Transfer& transfer, uint8_t* block_buffer);
    void Write(SdCard& card, const Transfer& transfer, uint8_t* block_buffer);
};
