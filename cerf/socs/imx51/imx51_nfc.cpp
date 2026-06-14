#include "imx51_nfc.h"
#include "imx51_nand_layout.h"

#include "../../boards/board_detector.h"
#include "../../boot/sec_flash.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

constexpr uint32_t kIpBase = 0x83FDB000u;
constexpr uint32_t kIpSize = 0x00001000u;

constexpr uint32_t kIpWrProtect = 0x00u;
constexpr uint32_t kIpUnlock0   = 0x04u;
constexpr uint32_t kIpUnlock7   = 0x20u;
constexpr uint32_t kIpCfg2      = 0x24u;
constexpr uint32_t kIpCfg3      = 0x28u;
constexpr uint32_t kIpIpc       = 0x2Cu;
constexpr uint32_t kIpAxiError  = 0x30u;
constexpr uint32_t kIpDelayLine = 0x34u;

constexpr uint32_t kAxiBase       = 0xCFFF0000u;
constexpr uint32_t kAxiWindowBase = 0xCFFF1000u;
constexpr uint32_t kAxiWindowSize = 0x00001000u;

constexpr uint32_t kAxiSpareBase = 0x1000u;
constexpr uint32_t kAxiSpareEnd  = 0x1200u;
constexpr uint32_t kAxiNandCmd   = 0x1E00u;
constexpr uint32_t kAxiNandAdd0  = 0x1E04u;
constexpr uint32_t kAxiCfg1      = 0x1E34u;
constexpr uint32_t kAxiLaunch    = 0x1E40u;

constexpr uint32_t kLaunchFcmd     = 1u << 0;
constexpr uint32_t kLaunchFadd     = 1u << 1;
constexpr uint32_t kLaunchFdiMask  = 1u << 2;
constexpr uint32_t kLaunchFdoMask  = 0x7u << 3;
constexpr uint32_t kLaunchAutoMask = 0xFFu << 6;

constexpr uint32_t kIpcInt  = 1u << 31;
constexpr uint32_t kIpcRbB  = 1u << 28;
constexpr uint32_t kIpcCack = 1u << 1;
constexpr uint32_t kIpcCreq = 1u << 0;

constexpr uint8_t  kNandCmdReadStart = 0x00u;

constexpr uint32_t kMainPageBytes = 0x1000u;

}  /* namespace */

REGISTER_SERVICE(Imx51Nfc);

bool Imx51Nfc::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    if (!bd || bd->GetSoc() != SocFamily::iMX51) return false;
    auto* sf = emu_.TryGet<SecFlash>();
    return sf && sf->IsPresent();
}

void Imx51Nfc::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t Imx51Nfc::MmioBase() const { return kIpBase; }
uint32_t Imx51Nfc::MmioSize() const { return kIpSize; }

uint8_t  Imx51Nfc::ReadByte (uint32_t addr) { return static_cast<uint8_t>(NfcRead(addr, 1)); }
uint16_t Imx51Nfc::ReadHalf (uint32_t addr) { return static_cast<uint16_t>(NfcRead(addr, 2)); }
uint32_t Imx51Nfc::ReadWord (uint32_t addr) { return NfcRead(addr, 4); }
void Imx51Nfc::WriteByte (uint32_t addr, uint8_t  value) { NfcWrite(addr, value, 1); }
void Imx51Nfc::WriteHalf (uint32_t addr, uint16_t value) { NfcWrite(addr, value, 2); }
void Imx51Nfc::WriteWord (uint32_t addr, uint32_t value) { NfcWrite(addr, value, 4); }

uint32_t Imx51Nfc::NfcRead(uint32_t addr, uint32_t width) {
    if (addr >= kAxiWindowBase && addr < kAxiWindowBase + kAxiWindowSize) {
        const uint32_t off = addr - kAxiBase;
        if (off >= kAxiSpareBase && off < kAxiSpareEnd) {
            uint32_t v = 0;
            for (uint32_t i = 0; i < width; ++i)
                v |= static_cast<uint32_t>(spare_[(off - kAxiSpareBase) + i]) << (8 * i);
            return v;
        }
        switch (off) {
            case kAxiNandCmd:  return nand_cmd_;
            case kAxiNandAdd0: return nand_add0_;
            case kAxiCfg1:     return cfg1_;
            case kAxiLaunch:   return launch_;
        }
        HaltUnsupportedAccess("NfcRead(AXI)", addr, 0);
    }

    const uint32_t off = addr - kIpBase;
    if (off >= kIpUnlock0 && off <= kIpUnlock7) return unlock_[(off - kIpUnlock0) / 4];
    switch (off) {
        case kIpWrProtect: return wr_protect_;
        case kIpCfg2:      return cfg2_;
        case kIpCfg3:      return cfg3_;
        case kIpIpc:
            return (int_pending_ ? kIpcInt : 0) | kIpcRbB |
                   (creq_ ? (kIpcCreq | kIpcCack) : 0);
        case kIpAxiError:  return axi_error_;
        case kIpDelayLine: return delay_line_;
    }
    HaltUnsupportedAccess("NfcRead(IP)", addr, 0);
}

void Imx51Nfc::NfcWrite(uint32_t addr, uint32_t value, uint32_t width) {
    if (addr >= kAxiWindowBase && addr < kAxiWindowBase + kAxiWindowSize) {
        const uint32_t off = addr - kAxiBase;
        if (off >= kAxiSpareBase && off < kAxiSpareEnd) {
            for (uint32_t i = 0; i < width; ++i)
                spare_[(off - kAxiSpareBase) + i] = static_cast<uint8_t>(value >> (8 * i));
            return;
        }
        switch (off) {
            case kAxiNandCmd:  nand_cmd_  = static_cast<uint8_t>(value); return;
            case kAxiNandAdd0: nand_add0_ = static_cast<uint8_t>(value); return;
            case kAxiCfg1:     cfg1_      = value;                       return;
            case kAxiLaunch:   Launch(value);                           return;
        }
        HaltUnsupportedAccess("NfcWrite(AXI)", addr, value);
    }

    const uint32_t off = addr - kIpBase;
    if (off >= kIpUnlock0 && off <= kIpUnlock7) { unlock_[(off - kIpUnlock0) / 4] = value; return; }
    switch (off) {
        case kIpWrProtect: wr_protect_ = value; return;
        case kIpCfg2:      cfg2_       = value; return;
        case kIpCfg3:      cfg3_       = value; return;
        case kIpIpc:
            int_pending_ = (value & kIpcInt) != 0;
            creq_        = (value & kIpcCreq) != 0;
            return;
        case kIpAxiError:  axi_error_  = value; return;
        case kIpDelayLine: delay_line_ = value; return;
    }
    HaltUnsupportedAccess("NfcWrite(IP)", addr, value);
}

void Imx51Nfc::Launch(uint32_t value) {
    launch_ = value;
    if (value & kLaunchFcmd) {
        if (nand_cmd_ == kNandCmdReadStart) addr_idx_ = 0;
        int_pending_ = true;
        return;
    }
    if (value & kLaunchFadd) {
        if (addr_idx_ < addr_bytes_.size()) addr_bytes_[addr_idx_++] = nand_add0_;
        int_pending_ = true;
        return;
    }
    if (value & kLaunchFdoMask) {
        ReadPage();
        int_pending_ = true;
        return;
    }
    if (value & (kLaunchFdiMask | kLaunchAutoMask)) {
        LOG(Caution, "Imx51Nfc: unhandled LAUNCH_NFC 0x%08X (cmd=0x%02X)\n",
            value, nand_cmd_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    int_pending_ = true;
}

uint64_t Imx51Nfc::FlashOffset() const {
    const uint32_t a0 = addr_bytes_[0], a1 = addr_bytes_[1];
    const uint32_t a2 = addr_bytes_[2], a3 = addr_bytes_[3], a4 = addr_bytes_[4];
    const uint32_t column = a0 | ((a1 & 0x0Fu) << 8);
    const uint32_t row    = a2 | (a3 << 8) | ((a4 & 0x03u) << 16);
    return (static_cast<uint64_t>(row) << 12) | column;
}

void Imx51Nfc::ReadPage() {
    const uint64_t phys_off = FlashOffset();
    std::array<uint8_t, kMainPageBytes> page{};
    page.fill(0xFFu);
    /* The `.sec` is packed by size; the device NAND is block-aligned. Map the
       physical NAND offset to its `.sec` source (nullopt = blank/erased block). */
    if (auto sec = emu_.Get<Imx51NandLayout>().PhysToSec(phys_off))
        emu_.Get<SecFlash>().ReadFlash(*sec, page.data(), page.size());
    /* spare[0x1C1] is the factory Bad Block Indicator (0xFF=good): the i.MX NFC
       chunked read relocates the BBI into a main-area byte and the stub swaps it
       back (i.MX NFC bad-block-indicator swap). Swap offsets 0xF4A/0x1C1 are from
       the stub's sub_0774 disassembly; a virtual NAND has no factory bad blocks. */
    spare_.fill(0xFF);
    std::swap(page[0xF4Au], spare_[0x1C1u]);
    emu_.Get<EmulatedMemory>().CopyIn(kAxiBase, page.data(), page.size());
}

void Imx51Nfc::SaveState(StateWriter& w) {
    w.WriteBytes(spare_.data(), spare_.size());
    w.Write(nand_cmd_);
    w.Write(nand_add0_);
    w.Write(cfg1_);
    w.Write(launch_);
    w.Write(wr_protect_);
    for (uint32_t v : unlock_) w.Write(v);
    w.Write(cfg2_);
    w.Write(cfg3_);
    w.Write(axi_error_);
    w.Write(delay_line_);
    w.Write<uint8_t>(int_pending_ ? 1 : 0);
    w.Write<uint8_t>(creq_ ? 1 : 0);
    w.WriteBytes(addr_bytes_.data(), addr_bytes_.size());
    w.Write(addr_idx_);
}

void Imx51Nfc::RestoreState(StateReader& r) {
    r.ReadBytes(spare_.data(), spare_.size());
    r.Read(nand_cmd_);
    r.Read(nand_add0_);
    r.Read(cfg1_);
    r.Read(launch_);
    r.Read(wr_protect_);
    for (uint32_t& v : unlock_) r.Read(v);
    r.Read(cfg2_);
    r.Read(cfg3_);
    r.Read(axi_error_);
    r.Read(delay_line_);
    uint8_t b = 0;
    r.Read(b); int_pending_ = b != 0;
    r.Read(b); creq_ = b != 0;
    r.ReadBytes(addr_bytes_.data(), addr_bytes_.size());
    r.Read(addr_idx_);
}
