#pragma once

#include "../../core/service.h"

#include <cstdint>

class MmcCard;

class Imx6UsdhcAdma final : public Service {
public:
    using Service::Service;

    struct Transfer {
        uint32_t descriptor_base;
        uint32_t block_size;
        uint32_t block_count;
        bool count_limited;
    };

    void Read(MmcCard& card, const Transfer& transfer, uint8_t* block_buffer);
    void Write(MmcCard& card, const Transfer& transfer, uint8_t* block_buffer);

private:
    void Walk(MmcCard& card, const Transfer& transfer, uint8_t* block_buffer, bool write);
};
