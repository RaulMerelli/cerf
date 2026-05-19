#include "rtl8019.h"

#include "../../core/log.h"

namespace {

const uint8_t kCisData[] = {
    0x01, 3, 0xDC, 0x00, 0xFF,
    0x17, 3, 0x49, 0x00, 0xFF,
    0x21, 2, 0x06, 0x03,
    0x15, 27, 0x04, 0x01, 0x50, 0x43, 0x4D, 0x43, 0x49, 0x41, 0x00,
              0x45, 0x74, 0x68, 0x65, 0x72, 0x6E, 0x65, 0x74, 0x20,
              0x43, 0x61, 0x72, 0x64, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x13, 3, 0x43, 0x49, 0x53,
    0x1A, 5, 0x01, 0x24, 0xF8, 0x03, 0x03,
    0x1B, 17, 0xE0, 0x81, 0x1D, 0x3F, 0x55, 0x4D, 0x5D, 0x06, 0x86,
              0x46, 0x26, 0xFC, 0x24, 0x65, 0x30, 0xFF, 0xFF,
    0x1B, 7, 0x20, 0x08, 0xCA, 0x60, 0x00, 0x03, 0x1F,
    0x1B, 7, 0x21, 0x08, 0xCA, 0x60, 0x20, 0x03, 0x1F,
    0x1B, 7, 0x22, 0x08, 0xCA, 0x60, 0x40, 0x03, 0x1F,
    0x1B, 7, 0x23, 0x08, 0xCA, 0x60, 0x60, 0x03, 0x1F,
    0x20, 4, 0x01, 0x8A, 0x00, 0x01,
    0x14, 0,
    0xFF, 0,
};
constexpr std::size_t kCisSize = sizeof(kCisData);

/* FCSR (Function Control / Status Register) at card-memory offset
   0x3FA per PCMCIA card-services spec. */
constexpr uint32_t kFcsrOffset = 0x3FA;

/* The BSP's pcmcia.c reads/writes two bytes at 0x3F8 / 0x3F9 to put
   the card into 16-bit-at-5V mode. The dev_emu chip accepts these
   silently. */
constexpr uint32_t kPwrCfgBase = 0x3F8;
constexpr uint32_t kPwrCfgEnd  = 0x3FA;

/* On-card RAM lives at card-memory offset 0x4000, 16 KB. Both the
   memory window and the DMA path through I/O offset 0x10 reach the
   same bytes here. */
constexpr uint32_t kRamBase = 0x4000;
constexpr uint32_t kRamSize = 0x4000;

/* FCSR bits. */
constexpr uint8_t kFcsrIntrAck = 0x01;
constexpr uint8_t kFcsrIntr    = 0x02;

}  /* namespace */

uint8_t Rtl8019::ReadMemoryByte(uint32_t offset) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (offset >= kRamBase && offset < kRamBase + kRamSize) {
        return card_ram_[offset - kRamBase];
    }
    if (offset == kFcsrOffset) {
        return fcsr_;
    }
    if (offset < kCisSize * 2u) {
        /* CIS ROM is 8-bit but the card is accessed in 16-bit mode.
           When that happens, each ROM byte appears at two
           consecutive even addresses — the driver compensates by
           multiplying ROM offsets by 2 before access. */
        return kCisData[offset / 2u];
    }
    if (offset >= kPwrCfgBase && offset < kPwrCfgEnd) {
        return 0u;
    }
    LOG(Caution, "[NE2000] read mem unsupported offset 0x%X\n", offset);
    return 0u;
}

uint16_t Rtl8019::ReadMemoryHalf(uint32_t offset) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (offset >= kRamBase && offset < kRamBase + kRamSize) {
        const uint32_t off = offset - kRamBase;
        return static_cast<uint16_t>(card_ram_[off]) |
               (static_cast<uint16_t>(card_ram_[off + 1]) << 8);
    }
    if (offset < kCisSize * 2u) {
        const uint16_t v = kCisData[offset / 2u];
        /* Both bytes of the returned halfword carry the same CIS
           byte — 8-bit ROM imaged on a 16-bit bus. */
        return static_cast<uint16_t>((v << 8) | v);
    }
    /* HwRamTest() probes 1 KB regions across the whole 64 KB window
       looking for RAM by writing a test pattern and reading it back.
       Off-range reads must return 0 (not halt) so the test correctly
       concludes "this region isn't RAM" and moves on. */
    return 0u;
}

void Rtl8019::WriteMemoryByte(uint32_t offset, uint8_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (offset >= kRamBase && offset < kRamBase + kRamSize) {
        card_ram_[offset - kRamBase] = value;
        return;
    }
    if (offset == kFcsrOffset) {
        fcsr_ = value;
        /* Driver acks the interrupt by writing FCSR_INTR_ACK; we
           clear FCSR_INTR + FCSR_INTR_ACK on receipt per the
           function-control spec. */
        if (fcsr_ & kFcsrIntrAck) {
            fcsr_ &= ~(kFcsrIntr | kFcsrIntrAck);
        }
        return;
    }
    if (offset >= kPwrCfgBase && offset < kPwrCfgEnd) {
        return;  /* 16-bit-at-5V config bytes — silently accepted */
    }
    LOG(Caution, "[NE2000] write mem unsupported offset 0x%X = 0x%02X\n",
        offset, value);
}

void Rtl8019::WriteMemoryHalf(uint32_t offset, uint16_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (offset >= kRamBase && offset < kRamBase + kRamSize) {
        const uint32_t off = offset - kRamBase;
        card_ram_[off]     = static_cast<uint8_t>(value & 0xFFu);
        card_ram_[off + 1] = static_cast<uint8_t>(value >> 8);
        return;
    }
    /* See ReadMemoryHalf comment — HwRamTest probes outside RAM and
       expects writes to silently no-op. */
}
