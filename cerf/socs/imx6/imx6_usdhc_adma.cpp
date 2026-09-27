#define NOMINMAX
#include "imx6_usdhc_adma.h"

#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../cpu/emulated_memory.h"
#include "../../peripherals/mmc/mmc_card.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kDescriptorBytes = 8u;
constexpr uint16_t kAttrValid = 0x0001u;
constexpr uint16_t kAttrEnd = 0x0002u;
constexpr uint32_t kActReserved = 1u;
constexpr uint32_t kActTran = 2u;
constexpr uint32_t kActLink = 3u;

}

/* i.MX 6Dual/6Quad Reference Manual Rev. 2, sections 67.4.2.4-67.4.2.4.3: ADMA2 descriptors
   carry Valid, End, Act, a byte length and an address; Act selects Nop, Tran or Link.
   QEMU hw/sd/sdhci.c ACT_LINK arm loads the next table from the descriptor address. */
void Imx6UsdhcAdma::Walk(MmcCard& card, const Transfer& transfer, uint8_t* block_buffer, bool write) {
    auto& memory = emu_.Get<EmulatedMemory>();
    auto& fatal = emu_.Get<Fatal>();
    uint32_t blocks_left = transfer.block_count;
    if (transfer.count_limited && blocks_left == 0u) return;

    uint32_t data_count = 0u;
    uint32_t table = transfer.descriptor_base;
    uint32_t offset = 0u;
    bool first_block = true;
    if (write) std::memset(block_buffer, 0, transfer.block_size);

    while (!transfer.count_limited || blocks_left > 0u) {
        const uint8_t* descriptor = memory.TryTranslateRange(table + offset, kDescriptorBytes);
        if (!descriptor)
            fatal.Die("uSDHC ADMA2 descriptor at 0x%08X is not mapped", table + offset);

        uint16_t attributes;
        uint16_t length16;
        uint32_t address;
        std::memcpy(&attributes, descriptor, sizeof(attributes));
        std::memcpy(&length16, descriptor + 2u, sizeof(length16));
        std::memcpy(&address, descriptor + 4u, sizeof(address));
        if (!(attributes & kAttrValid))
            fatal.Die("uSDHC ADMA2 descriptor at 0x%08X is not valid (0x%04X)", table + offset, attributes);

        const bool end = (attributes & kAttrEnd) != 0u;
        const uint32_t act = (attributes >> 4) & 0x03u;
        if (act == kActLink) {
            table = address;
            offset = 0u;
            continue;
        }
        if (act == kActReserved)
            fatal.Die("uSDHC ADMA2 reserved Act encoding at 0x%08X (0x%04X)", table + offset, attributes);
        if (act == kActTran) {
            const uint32_t length = length16 ? static_cast<uint32_t>(length16) : 65536u;
            uint32_t moved = 0u;
            while (moved < length && (!transfer.count_limited || blocks_left > 0u)) {
                if (!write && data_count == 0u) {
                    if (!first_block) card.NextBlock();
                    first_block = false;
                    const std::vector<uint8_t>& staged = card.ReadData();
                    if (staged.size() < transfer.block_size)
                        fatal.Die("uSDHC ADMA2 read wants %u bytes and the card staged %zu",
                                  transfer.block_size, staged.size());
                    std::memcpy(block_buffer, staged.data(), transfer.block_size);
                }
                const uint32_t count = std::min(length - moved, transfer.block_size - data_count);
                uint8_t* host = memory.TryTranslateRange(address + moved, count, write);
                if (!host)
                    fatal.Die("uSDHC ADMA2 buffer 0x%08X+%u is not mapped for %s", address, moved,
                              write ? "read" : "write");
                if (write)
                    std::memcpy(block_buffer + data_count, host, count);
                else
                    std::memcpy(host, block_buffer + data_count, count);
                data_count += count;
                moved += count;
                if (data_count == transfer.block_size) {
                    if (write) {
                        card.ReceiveBlock(block_buffer, transfer.block_size);
                        std::memset(block_buffer, 0, transfer.block_size);
                    }
                    data_count = 0u;
                    if (transfer.count_limited) --blocks_left;
                }
            }
        }
        if (end) break;
        offset += kDescriptorBytes;
    }
}

void Imx6UsdhcAdma::Read(MmcCard& card, const Transfer& transfer, uint8_t* block_buffer) {
    Walk(card, transfer, block_buffer, /*write=*/false);
}

void Imx6UsdhcAdma::Write(MmcCard& card, const Transfer& transfer, uint8_t* block_buffer) {
    Walk(card, transfer, block_buffer, /*write=*/true);
}

REGISTER_SERVICE(Imx6UsdhcAdma);
