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
constexpr uint32_t kAxiNandAdd11 = 0x1E30u;
constexpr uint32_t kAxiCfg1      = 0x1E34u;
constexpr uint32_t kAxiEccStatus = 0x1E38u;
constexpr uint32_t kAxiStatusSum = 0x1E3Cu;
constexpr uint32_t kAxiLaunch    = 0x1E40u;

constexpr uint32_t kLaunchFcmd     = 1u << 0;
constexpr uint32_t kLaunchFadd     = 1u << 1;
constexpr uint32_t kLaunchFdiMask  = 1u << 2;
constexpr uint32_t kLaunchFdoMask  = 0x7u << 3;
constexpr uint32_t kLaunchAutoRead = 1u << 7;   /* AUTO_READ (RM Table 45-29) */
constexpr uint32_t kLaunchAutoMask = 0xFFu << 6;

constexpr uint32_t kIpcInt  = 1u << 31;
constexpr uint32_t kIpcRbB  = 1u << 28;
constexpr uint32_t kIpcCack = 1u << 1;
constexpr uint32_t kIpcCreq = 1u << 0;

constexpr uint8_t  kNandCmdReadStart = 0x00u;
constexpr uint8_t  kNandCmdReadId    = 0x90u;

constexpr uint32_t kMainPageBytes = 0x1000u;

/* READ ID (0x90) response. mfr 0x2C (Micron) + device 0x48 (the 2 GB 4 KB-page
   512 KB-block part matching Imx51NandLayout) are grounded from SBOOT's own
   media-ID table at Bootloader.bin 0x8FF0A94C, which matches on these two bytes;
   the extended-ID geometry bytes are unused by SBOOT and stay 0. */
constexpr std::array<uint8_t, 5> kReadIdBytes = {0x2Cu, 0x48u, 0x00u, 0x00u, 0x00u};

/* Decode the five NAND address cycles (little-endian [col_lo, col_hi, row_lo,
   row_mid, row_hi]) to a linear flash byte offset: 12-bit column (4 KB page) +
   19-bit row (the 2 GB Micron part = 0x80000 pages; READ ID 0x2C/0x48 above). */
uint64_t DecodeNandAddr(uint8_t a0, uint8_t a1, uint8_t a2, uint8_t a3, uint8_t a4) {
    const uint32_t column = a0 | ((a1 & 0x0Fu) << 8);
    const uint32_t row    = a2 | (a3 << 8) | ((a4 & 0x07u) << 16);
    return (static_cast<uint64_t>(row) << 12) | column;
}

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
        if (off >= kAxiNandAdd0 && off <= kAxiNandAdd11)
            return nand_add_[(off - kAxiNandAdd0) / 4];
        switch (off) {
            case kAxiNandCmd:   return nand_cmd_;
            case kAxiCfg1:      return cfg1_;
            case kAxiEccStatus: return ecc_status_;
            case kAxiStatusSum: return status_sum_;
            case kAxiLaunch:    return launch_;
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
        if (off >= kAxiNandAdd0 && off <= kAxiNandAdd11) {
            nand_add_[(off - kAxiNandAdd0) / 4] = value; return;
        }
        switch (off) {
            case kAxiNandCmd:   nand_cmd_   = static_cast<uint8_t>(value); return;
            case kAxiCfg1:      cfg1_       = value;                       return;
            case kAxiEccStatus: ecc_status_ = value;                      return;
            case kAxiStatusSum: status_sum_ = value;                      return;
            case kAxiLaunch:    Launch(value);                            return;
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
        if (nand_cmd_ == kNandCmdReadStart)   { addr_idx_ = 0; read_id_ = false; }
        else if (nand_cmd_ == kNandCmdReadId) { addr_idx_ = 0; read_id_ = true;  }
        int_pending_ = true;
        return;
    }
    if (value & kLaunchFadd) {
        if (addr_idx_ < addr_bytes_.size())
            addr_bytes_[addr_idx_++] = static_cast<uint8_t>(nand_add_[0]);
        int_pending_ = true;
        return;
    }
    if (value & kLaunchFdoMask) {
        if (read_id_) ReadId();
        else          ReadPage();
        int_pending_ = true;
        return;
    }
    if (value & kLaunchAutoRead) {
        AutoRead();
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
    return DecodeNandAddr(addr_bytes_[0], addr_bytes_[1], addr_bytes_[2],
                          addr_bytes_[3], addr_bytes_[4]);
}

void Imx51Nfc::FillPageBuffer(uint64_t flash_off, bool relocate_bbi) {
    std::array<uint8_t, kMainPageBytes> page{};
    page.fill(0xFFu);
    spare_.fill(0xFF);   /* virtual NAND: no factory bad blocks -> spare all good */
    /* The `.sec` is packed by size; the device NAND is block-aligned. Map the
       physical NAND offset to its `.sec` source (nullopt = blank/erased block).
       The tail blocks serve synthesized device-state structures the `.sec` lacks:
       the BBT (sets main+spare) and the DPS manifest. */
    auto& layout = emu_.Get<Imx51NandLayout>();
    if (layout.IsBbtBlock(flash_off))
        layout.BuildBbtPage(flash_off, page.data(), page.size(),
                            spare_.data(), spare_.size());
    else if (layout.IsDpsOffset(flash_off))
        layout.BuildDpsPage(flash_off, page.data(), page.size(),
                            spare_.data(), spare_.size());
    else if (auto sec = layout.PhysToSec(flash_off))
        emu_.Get<SecFlash>().ReadFlash(*sec, page.data(), page.size());
    /* Stub-only: the first-stage stub's NFC config relocates the factory BBI to
       main byte 0xF4A and sub_0774 swaps it back, so the stub path must pre-swap.
       The FMD reads main data raw (Bootloader.bin 0x8FF090F0 copies 0xCFFF0000+n*512
       straight out, no swap), so swapping on AUTO_READ would corrupt page[0xF4A]. */
    if (relocate_bbi) std::swap(page[0xF4Au], spare_[0x1C1u]);
    emu_.Get<EmulatedMemory>().CopyIn(kAxiBase, page.data(), page.size());
}

void Imx51Nfc::ReadPage() { FillPageBuffer(FlashOffset(), /*relocate_bbi=*/true); }

void Imx51Nfc::AutoRead() {
    /* AUTO_READ: full page-read into the internal RAM buffer (MCIMX51RM §45.9.1.3).
       A 4 KB page forces RBA=000 (NFC_CONFIGURATION1, Table 45-26), so the page
       lands at the AXI buffer base; a non-000 RBA would need a sub-buffer offset. */
    const uint32_t iterations = (cfg1_ >> 8) & 0xFu;
    if (iterations != 0) {
        LOG(Caution, "Imx51Nfc: AUTO_READ NUM_OF_ITERATIONS=%u unsupported (cfg1=0x%08X)\n",
            iterations, cfg1_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    FillPageBuffer(AutoReadFlashOffset(), /*relocate_bbi=*/false);
}

uint64_t Imx51Nfc::AutoReadFlashOffset() const {
    /* AUTO_READ address from the active group NAND_ADDRESS0 (Table 45-11):
       NAND_ADD0 = ADDRESS0[31:0], NAND_ADD8[7:0] = ADDRESS0[39:32]. */
    return DecodeNandAddr(static_cast<uint8_t>(nand_add_[0]),
                          static_cast<uint8_t>(nand_add_[0] >> 8),
                          static_cast<uint8_t>(nand_add_[0] >> 16),
                          static_cast<uint8_t>(nand_add_[0] >> 24),
                          static_cast<uint8_t>(nand_add_[8]));
}

void Imx51Nfc::ReadId() {
    std::array<uint8_t, kMainPageBytes> buf{};
    std::copy(kReadIdBytes.begin(), kReadIdBytes.end(), buf.begin());
    emu_.Get<EmulatedMemory>().CopyIn(kAxiBase, buf.data(), buf.size());
}

void Imx51Nfc::SaveState(StateWriter& w) {
    w.WriteBytes(spare_.data(), spare_.size());
    w.Write(nand_cmd_);
    for (uint32_t v : nand_add_) w.Write(v);
    w.Write(cfg1_);
    w.Write(ecc_status_);
    w.Write(status_sum_);
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
    w.Write<uint8_t>(read_id_ ? 1 : 0);
}

void Imx51Nfc::RestoreState(StateReader& r) {
    r.ReadBytes(spare_.data(), spare_.size());
    r.Read(nand_cmd_);
    for (uint32_t& v : nand_add_) r.Read(v);
    r.Read(cfg1_);
    r.Read(ecc_status_);
    r.Read(status_sum_);
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
    uint8_t rid = 0;
    r.Read(rid); read_id_ = rid != 0;
}
