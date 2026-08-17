#define NOMINMAX
#include "imx6_usdhc_adma.h"

#include "../../core/cerf_emulator.h"
#include "../../cpu/emulated_memory.h"
#include "../../peripherals/sd_card/sd_card.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

/* i.MX 6Dual/6Quad Reference Manual Rev. 2, sections 67.4.2.4-67.4.2.4.3:
   ADMA2 Tran descriptors carry byte length and address; Valid gates the
   descriptor and End terminates the table. */
void Imx6UsdhcAdma::Read(SdCard& card,
                         const Transfer& transfer,
                         uint8_t* block_buffer) {
    auto& memory = emu_.Get<EmulatedMemory>();
    uint32_t blocks_left = transfer.block_count;
    if (transfer.count_limited && blocks_left == 0u)
        return;

    uint32_t data_count = 0u;
    for (uint32_t offset = 0u;
         !transfer.count_limited || blocks_left > 0u;
         offset += 8u) {
        const uint8_t* descriptor =
            memory.TryTranslate(transfer.descriptor_base + offset);
        if (!descriptor)
            break;

        uint16_t attributes;
        uint16_t length16;
        uint32_t address;
        std::memcpy(&attributes, descriptor, sizeof(attributes));
        std::memcpy(&length16, descriptor + 2u, sizeof(length16));
        std::memcpy(&address, descriptor + 4u, sizeof(address));
        if (!(attributes & 0x01u))
            break;

        const bool end = (attributes & 0x02u) != 0u;
        if (((attributes >> 4) & 0x03u) == 0x02u) {
            const uint32_t length = length16 ? static_cast<uint32_t>(length16)
                                             : 65536u;
            uint32_t moved = 0u;
            while (moved < length &&
                   (!transfer.count_limited || blocks_left > 0u)) {
                if (data_count == 0u)
                    card.ReadBlock(block_buffer);
                const uint32_t count = std::min(
                    length - moved, transfer.block_size - data_count);
                if (uint8_t* destination = memory.TryTranslate(address + moved))
                    std::memcpy(destination, block_buffer + data_count, count);
                data_count += count;
                moved += count;
                if (data_count == transfer.block_size) {
                    data_count = 0u;
                    if (transfer.count_limited)
                        --blocks_left;
                }
            }
        }
        if (end)
            break;
    }
}

void Imx6UsdhcAdma::Write(SdCard& card,
                          const Transfer& transfer,
                          uint8_t* block_buffer) {
    auto& memory = emu_.Get<EmulatedMemory>();
    uint32_t blocks_left = transfer.block_count;
    if (transfer.count_limited && blocks_left == 0u)
        return;

    uint32_t data_count = 0u;
    std::memset(block_buffer, 0, 512u);
    for (uint32_t offset = 0u;
         !transfer.count_limited || blocks_left > 0u;
         offset += 8u) {
        const uint8_t* descriptor =
            memory.TryTranslate(transfer.descriptor_base + offset);
        if (!descriptor)
            break;

        uint16_t attributes;
        uint16_t length16;
        uint32_t address;
        std::memcpy(&attributes, descriptor, sizeof(attributes));
        std::memcpy(&length16, descriptor + 2u, sizeof(length16));
        std::memcpy(&address, descriptor + 4u, sizeof(address));
        if (!(attributes & 0x01u))
            break;

        const bool end = (attributes & 0x02u) != 0u;
        if (((attributes >> 4) & 0x03u) == 0x02u) {
            const uint32_t length = length16 ? static_cast<uint32_t>(length16)
                                             : 65536u;
            uint32_t moved = 0u;
            while (moved < length &&
                   (!transfer.count_limited || blocks_left > 0u)) {
                const uint32_t count = std::min(
                    length - moved, transfer.block_size - data_count);
                if (const uint8_t* source = memory.TryTranslate(address + moved))
                    std::memcpy(block_buffer + data_count, source, count);
                else
                    std::memset(block_buffer + data_count, 0, count);
                data_count += count;
                moved += count;
                if (data_count == transfer.block_size) {
                    card.WriteBlock(block_buffer);
                    std::memset(block_buffer, 0, 512u);
                    data_count = 0u;
                    if (transfer.count_limited)
                        --blocks_left;
                }
            }
        }
        if (end)
            break;
    }
}

REGISTER_SERVICE(Imx6UsdhcAdma);
