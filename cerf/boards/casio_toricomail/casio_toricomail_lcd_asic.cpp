#include "casio_toricomail_lcd_asic.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstring>

bool CasioToricomailLcdAsic::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::CasioToricomail;
}

void CasioToricomailLcdAsic::OnReady() {
    fb_.assign(kFbSize, 0u);
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* sub_9F0B7D20 and sub_9F0B6E78 fill dst 0, width 320, height 240 at the 1024-byte pitch. */
void CasioToricomailLcdAsic::RunFillLocked() {
    if (fill_dst_ != 0 || fill_width_ != kVisibleW || fill_height_ != kVisibleH) {
        LOG(Caution, "CasioToricomailLcdAsic: fill geometry dst=0x%X w=%u h=%u not modeled\n",
            fill_dst_, fill_width_, fill_height_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    for (uint32_t y = 0; y < kVisibleH; ++y) {
        uint8_t* row = fb_.data() + static_cast<size_t>(y) * kPitchBytes;
        for (uint32_t x = 0; x < kVisibleW; ++x)
            std::memcpy(row + x * 2u, &fill_value_, sizeof(fill_value_));
    }
}

uint16_t CasioToricomailLcdAsic::ReadReg(uint32_t off) {
    switch (off) {
        /* nk.exe 0x9F0B70D4 reads 0x40A as a posted-write flush; sub_9F0B8720 RMWs 0x406
           (3-wire DAC). */
        case 0x40A: return reg_40a_;
        case 0x406: return reg_406_;
        /* sub_9F0B7D20 polls 0x218 / 0x21A bit0 until the fill (run in the write) clears it. */
        case 0x218: return 0;
        case 0x21A: return 0;
        /* ISR 0x9F0BA2C4 reads STATUS 0x1008 and ENABLE 0x100A. */
        case 0x1008: return int_status_;
        case 0x100A: return int_enable_;
        /* sub_9F0B8720 reads 0x1134 bit0 (reboot vs HIBERNATE). */
        case 0x1134: return reg_1134_;
        /* sub_9F0B8720 saves 0x010-0x01E to DRAM at suspend; RMW's 0x1130 (|=2 then &=~2). */
        case 0x010: case 0x012: case 0x014: case 0x016:
        case 0x018: case 0x01A: case 0x01C: case 0x01E:
            return suspend_save_[(off - 0x010u) / 2u];
        case 0x1130: return reg_1130_;
        default:
            HaltUnsupportedAccess("Casio LCD ASIC ReadReg", kBase + off, 0);
    }
}

void CasioToricomailLcdAsic::WriteReg(uint32_t off, uint16_t value) {
    /* sub_9F0B6E78 panel-enable / display-latch. */
    if (off == 0x7A0 || off == 0x7A2) { (off == 0x7A0 ? reg_7a0_ : reg_7a2_) = value; return; }
    if (off == 0x7A4) { reg_7a4_ = value; return; }

    /* sub_9F0B6E78 writes timing/DAC/flush 0x400-0x410, grayscale 0x5E4, gamma 0x600-0x62C. */
    if (off == 0x40A) { reg_40a_ = value; return; }
    if (off == 0x406) { reg_406_ = value; return; }
    if (off >= 0x400 && off <= 0x410 && (off & 1) == 0) return;
    if (off == 0x5E4) return;
    if (off >= 0x600 && off <= 0x62C && (off & 1) == 0) return;

    /* sub_9F0B7D20 fill engine: dst 0x200, value 0x202, width 0x204, height 0x206;
       0x21C=1 / 0x21E=0 are the only config the callers write. */
    switch (off) {
        case 0x200: fill_dst_    = value; return;
        case 0x202: fill_value_  = value; return;
        case 0x204: fill_width_  = value; return;
        case 0x206: fill_height_ = value; return;
        case 0x218: if (value & 1u) RunFillLocked(); return;
        case 0x21A: if (value & 1u) RunFillLocked(); return;
        case 0x21C: if (value != 1u) break; return;
        case 0x21E: if (value != 0u) break; return;
        /* ISR acks by writing STATUS back with the handled cause bits cleared. */
        case 0x1008: int_status_ = value; return;
        case 0x100A: int_enable_ = value; return;
        /* StartUp 0x9F0B5BCC: 0x1002=0x108, 0x1010=3, 0x1134=0. */
        case 0x1002: reg_1002_ = value; return;
        case 0x1010: reg_1010_ = value; return;
        case 0x1134: reg_1134_ = value; return;
        /* sub_9F0B8720 suspend: 0x010-0x01E save/restore; 0x1130 RMW. */
        case 0x010: case 0x012: case 0x014: case 0x016:
        case 0x018: case 0x01A: case 0x01C: case 0x01E:
            suspend_save_[(off - 0x010u) / 2u] = value; return;
        case 0x1130: reg_1130_ = value; return;
        /* sub_9F0B8720 power-down: writes 0x300/0x1000/0xB00/0xB10/0x1114/0xA00/0xE0C/0x712. */
        case 0x300: case 0x1000: case 0xB00: case 0xB10:
        case 0x1114: case 0xA00: case 0xE0C: case 0x712:
            return;
        default: break;
    }
    HaltUnsupportedAccess("Casio LCD ASIC WriteReg", kBase + off, value);
}

uint8_t CasioToricomailLcdAsic::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) return fb_[off - kFbOffset];
    HaltUnsupportedAccess("Casio LCD ASIC ReadByte", addr, 0);
}

uint16_t CasioToricomailLcdAsic::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) { uint16_t v; std::memcpy(&v, &fb_[off - kFbOffset], sizeof(v)); return v; }
    return ReadReg(off);
}

uint32_t CasioToricomailLcdAsic::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) { uint32_t v; std::memcpy(&v, &fb_[off - kFbOffset], sizeof(v)); return v; }
    HaltUnsupportedAccess("Casio LCD ASIC ReadWord", addr, 0);
}

void CasioToricomailLcdAsic::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) { fb_[off - kFbOffset] = value; return; }
    HaltUnsupportedAccess("Casio LCD ASIC WriteByte", addr, value);
}

void CasioToricomailLcdAsic::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) { std::memcpy(&fb_[off - kFbOffset], &value, sizeof(value)); return; }
    WriteReg(off, value);
}

void CasioToricomailLcdAsic::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) { std::memcpy(&fb_[off - kFbOffset], &value, sizeof(value)); return; }
    HaltUnsupportedAccess("Casio LCD ASIC WriteWord", addr, value);
}

void CasioToricomailLcdAsic::SaveState(StateWriter& w) {
    w.Write<uint64_t>(fb_.size());
    if (!fb_.empty()) w.WriteBytes(fb_.data(), fb_.size());
    w.Write(fill_dst_); w.Write(fill_value_); w.Write(fill_width_); w.Write(fill_height_);
    w.Write(int_status_); w.Write(int_enable_);
    w.Write(reg_1002_); w.Write(reg_1010_); w.Write(reg_1134_);
    w.Write(reg_7a0_); w.Write(reg_7a2_); w.Write(reg_7a4_);
    w.Write(reg_40a_); w.Write(reg_406_);
    for (uint16_t v : suspend_save_) w.Write(v);
    w.Write(reg_1130_);
}

void CasioToricomailLcdAsic::RestoreState(StateReader& r) {
    uint64_t n = 0; r.Read(n);
    fb_.assign(static_cast<size_t>(n), 0u);
    if (n) r.ReadBytes(fb_.data(), static_cast<size_t>(n));
    r.Read(fill_dst_); r.Read(fill_value_); r.Read(fill_width_); r.Read(fill_height_);
    r.Read(int_status_); r.Read(int_enable_);
    r.Read(reg_1002_); r.Read(reg_1010_); r.Read(reg_1134_);
    r.Read(reg_7a0_); r.Read(reg_7a2_); r.Read(reg_7a4_);
    r.Read(reg_40a_); r.Read(reg_406_);
    for (uint16_t& v : suspend_save_) r.Read(v);
    r.Read(reg_1130_);
}

REGISTER_SERVICE(CasioToricomailLcdAsic);
