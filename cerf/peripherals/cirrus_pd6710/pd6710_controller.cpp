#include "pd6710_controller.h"

#include "pd6710_card_irq_line.h"
#include "pd6710_management_irq_line.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../host/host_widget_registry.h"
#include "../../state/state_stream.h"

namespace {

constexpr uint32_t kPortIndex = 0x0u;
constexpr uint32_t kPortData  = 0x1u;

/* PCIC register indices, BSP pd6710.h. */
enum : uint8_t {
    kRegChipRevision            = 0x00,
    kRegInterfaceStatus         = 0x01,
    kRegPowerControl            = 0x02,
    kRegInterruptAndGeneralCtrl = 0x03,
    kRegCardStatusChange        = 0x04,
    kRegStatusChangeIntConfig   = 0x05,
    kRegWindowEnable            = 0x06,
    kRegIoWindowControl         = 0x07,
    kRegIoMapFirst              = 0x08,   /* 0x08-0x0F: io start/end ×2 */
    kRegIoMapLast               = 0x0F,
    kRegMemMapFirst             = 0x10,   /* 0x10-0x35 stride 8, low 6 */
    kRegMemMapLast              = 0x35,
    kRegGeneralControl          = 0x16,
    kRegFifoControl             = 0x17,
    kRegGlobalControl           = 0x1E,
    kRegChipInfo                = 0x1F,
    kRegIoOffsetFirst           = 0x36,   /* 0x36-0x39: io offsets ×2 */
    kRegIoOffsetLast            = 0x39,
    kRegTimingFirst             = 0x3A,   /* 0x3A-0x3F: setup/cmd/recovery ×2 */
    kRegTimingLast              = 0x3F,
};

/* REG_INTERFACE_STATUS bits, BSP pd6710.h. */
constexpr uint8_t kIfsCd1       = 0x04u;
constexpr uint8_t kIfsCd2       = 0x08u;
constexpr uint8_t kIfsCardReady = 0x20u;

/* PWR_VCC_POWER | PWR_OUTPUT_ENABLE — the powered-on pattern the BSP
   driver writes (pd6710.h bits 0x10 / 0x80). */
constexpr uint8_t kPowerOnPattern = 0x90u;

/* REG_INTERRUPT_AND_GENERAL_CONTROL low nibble = card IRQ select; any
   non-zero value enables the card IRQ routing. */
constexpr uint8_t kCardIrqSelectMask = 0x0Fu;

/* REG_CARD_STATUS_CHANGE / REG_STATUS_CHANGE_INT_CONFIG detect bits,
   BSP pd6710.h (CSC_DETECT_CHANGE / CFG_CARD_DETECT_ENABLE). */
constexpr uint8_t kCscDetectChange     = 0x08u;
constexpr uint8_t kCfgCardDetectEnable = 0x08u;

/* Card Memory Map Offset Address High bits, PD6710 datasheet p56
   (§8.6): bit 6 = REG setting (attribute), bit 7 = write protect. */
constexpr uint8_t kMohRegActive    = 0x40u;
constexpr uint8_t kMohWriteProtect = 0x80u;

bool IsMemMapReg(uint8_t index) {
    return index >= kRegMemMapFirst && index <= kRegMemMapLast &&
           (index & 7u) <= 5u;
}

}  /* namespace */

Pd6710Controller::Pd6710Controller(CerfEmulator& emu)
    : Service(emu), slot_(emu, *this, L"PCMCIA #1") {}

bool Pd6710Controller::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
}

void Pd6710Controller::OnReady() {
    /* Power-on default: first REG_CHIP_INFO read returns 0xC0, later
       reads 0 until rewritten (mirrors the DeviceEmulator host
       runtime; the BSP driver probes this pattern to detect the chip). */
    reg_chip_info_ = 0xC0u;

    emu_.Get<HostWidgetRegistry>().Register(&slot_);
}

bool Pd6710Controller::IsCardPoweredLocked() const {
    return (reg_power_control_ & kPowerOnPattern) == kPowerOnPattern;
}

uint8_t Pd6710Controller::ReadPcicByte(uint32_t port) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (port == kPortIndex) return index_;
    return ReadIndexedDataLocked();
}

void Pd6710Controller::WritePcicByte(uint32_t port, uint8_t value) {
    bool power_changed = false;
    bool power_on      = false;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (port == kPortIndex) {
            index_ = value;
            return;
        }
        power_changed = WriteIndexedDataLocked(value, &power_on);
    }
    if (power_changed) slot_.SetPowered(power_on);
}

uint8_t Pd6710Controller::ReadIndexedDataLocked() {
    if (IsMemMapReg(index_)) {
        return mem_reg_[(index_ - kRegMemMapFirst) >> 3][index_ & 7u];
    }
    if (index_ >= kRegIoMapFirst && index_ <= kRegIoMapLast) {
        const int win = (index_ - kRegIoMapFirst) >= 4 ? 1 : 0;
        switch ((index_ - kRegIoMapFirst) & 3u) {
            case 0: return io_start_lo_[win];
            case 1: return io_start_hi_[win];
            case 2: return io_end_lo_[win];
            case 3: return io_end_hi_[win];
        }
    }
    if (index_ >= kRegIoOffsetFirst && index_ <= kRegIoOffsetLast) {
        const int win = (index_ - kRegIoOffsetFirst) >= 2 ? 1 : 0;
        return ((index_ - kRegIoOffsetFirst) & 1u) ? io_off_hi_[win]
                                                   : io_off_lo_[win];
    }
    if (index_ >= kRegTimingFirst && index_ <= kRegTimingLast) {
        return timing_[index_ - kRegTimingFirst];
    }

    switch (index_) {
        case kRegChipRevision:
            return 0x83u;   /* the BSP driver expects this revision */

        case kRegInterfaceStatus: {
            uint8_t v = 0u;
            if (slot_.HasCard()) {
                v |= kIfsCd1 | kIfsCd2;
                if (IsCardPoweredLocked()) v |= kIfsCardReady;
            }
            return v;
        }

        case kRegPowerControl:          return reg_power_control_;
        case kRegInterruptAndGeneralCtrl: return reg_interrupt_and_gen_ctrl_;

        case kRegCardStatusChange: {
            const uint8_t v = reg_card_status_change_;
            reg_card_status_change_ = 0u;   /* read clears */
            return v;
        }

        case kRegStatusChangeIntConfig: return reg_status_change_int_cfg_;
        case kRegWindowEnable:          return reg_window_enable_;
        case kRegIoWindowControl:       return reg_io_window_control_;

        case kRegGeneralControl:        return 1u;  /* 5.0 V → 16-bit card */
        case kRegFifoControl:           return 0u;
        case kRegGlobalControl:         return 0u;

        case kRegChipInfo: {
            const uint8_t v = reg_chip_info_;
            reg_chip_info_ = static_cast<uint8_t>(reg_chip_info_ & ~0xC0u);
            return v;
        }

        default:
            LOG(Caution, "[PD6710] read of unmodeled INDEX 0x%02X\n", index_);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

bool Pd6710Controller::WriteIndexedDataLocked(uint8_t value, bool* power_on) {
    if (IsMemMapReg(index_)) {
        mem_reg_[(index_ - kRegMemMapFirst) >> 3][index_ & 7u] = value;
        return false;
    }
    if (index_ >= kRegIoMapFirst && index_ <= kRegIoMapLast) {
        const int win = (index_ - kRegIoMapFirst) >= 4 ? 1 : 0;
        switch ((index_ - kRegIoMapFirst) & 3u) {
            case 0: io_start_lo_[win] = value; break;
            case 1: io_start_hi_[win] = value; break;
            case 2: io_end_lo_[win]   = value; break;
            case 3: io_end_hi_[win]   = value; break;
        }
        return false;
    }
    if (index_ >= kRegIoOffsetFirst && index_ <= kRegIoOffsetLast) {
        const int win = (index_ - kRegIoOffsetFirst) >= 2 ? 1 : 0;
        if ((index_ - kRegIoOffsetFirst) & 1u) io_off_hi_[win] = value;
        else                                   io_off_lo_[win] = value;
        return false;
    }
    if (index_ >= kRegTimingFirst && index_ <= kRegTimingLast) {
        timing_[index_ - kRegTimingFirst] = value;
        return false;
    }

    switch (index_) {
        case kRegPowerControl: {
            const bool was = IsCardPoweredLocked();
            reg_power_control_ = value;
            const bool now = IsCardPoweredLocked();
            *power_on = now;
            return now != was;
        }

        case kRegInterruptAndGeneralCtrl:
            reg_interrupt_and_gen_ctrl_ = value;
            return false;
        case kRegStatusChangeIntConfig:
            reg_status_change_int_cfg_ = value;
            return false;
        case kRegWindowEnable:
            reg_window_enable_ = value;
            return false;
        case kRegIoWindowControl:
            reg_io_window_control_ = value;
            return false;

        /* FIFO flush / MISC controls: accepted, no emulated effect
           (mirrors the DeviceEmulator host runtime). */
        case kRegGeneralControl:
        case kRegFifoControl:
        case kRegGlobalControl:
            return false;

        case kRegChipInfo:
            reg_chip_info_ = 0xC0u;
            return false;

        default:
            LOG(Caution, "[PD6710] write of unmodeled INDEX 0x%02X "
                    "(value 0x%02X)\n", index_, value);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

bool Pd6710Controller::MapIo(uint32_t bus_io, uint32_t* card_io) {
    std::lock_guard<std::mutex> lk(state_mutex_);

    /* WIN_IO_MAP0_ENABLE = bit 6, WIN_IO_MAP1_ENABLE = bit 7 (BSP
       pd6710.h). Window 1 checked first, matching the DeviceEmulator
       host runtime's decode order. */
    constexpr uint8_t kWinIoMap0Enable = 0x40u;
    constexpr uint8_t kWinIoMap1Enable = 0x80u;

    auto u16 = [](uint8_t lo, uint8_t hi) -> uint32_t {
        return (uint32_t)lo | ((uint32_t)hi << 8);
    };

    for (int win : {1, 0}) {
        const uint8_t enable_bit =
            (win == 1) ? kWinIoMap1Enable : kWinIoMap0Enable;
        if (!(reg_window_enable_ & enable_bit)) continue;
        const uint32_t start = u16(io_start_lo_[win], io_start_hi_[win]);
        const uint32_t end   = u16(io_end_lo_[win],   io_end_hi_[win]);
        if (bus_io < start || bus_io > end) continue;
        /* Card I/O address = bus address + offset; offset bit 0 must
           be 0 (PD6710 datasheet p52, §7.6). */
        const uint32_t offset =
            u16(io_off_lo_[win], io_off_hi_[win]) & 0xFFFEu;
        *card_io = (bus_io + offset) & 0xFFFFu;
        return true;
    }
    return false;
}

bool Pd6710Controller::MapMem(uint32_t bus_off, uint32_t* card_addr,
                              bool* attribute, bool* writable) {
    std::lock_guard<std::mutex> lk(state_mutex_);

    const uint32_t page = (bus_off >> 12) & 0xFFFu;   /* host bits 23:12 */

    for (int win = 0; win < 5; ++win) {
        if (!(reg_window_enable_ & (1u << win))) continue;
        const uint8_t* r = mem_reg_[win];
        const uint32_t start = (uint32_t)r[0] | ((uint32_t)(r[1] & 0x0Fu) << 8);
        const uint32_t end   = (uint32_t)r[2] | ((uint32_t)(r[3] & 0x0Fu) << 8);
        if (page < start || page > end) continue;
        /* Card address = host address + (offset << 12), offset is
           bits 25:12, card space 26 bits (PD6710 datasheet p56
           §8.5-8.6: the offset is added to the host memory address
           to form the PC Card memory address). */
        const uint32_t offset =
            (uint32_t)r[4] | ((uint32_t)(r[5] & 0x3Fu) << 8);
        *card_addr = (bus_off + (offset << 12)) & 0x3FFFFFFu;
        *attribute = (r[5] & kMohRegActive)    != 0u;
        *writable  = (r[5] & kMohWriteProtect) == 0u;
        return true;
    }
    return false;
}

void Pd6710Controller::OnCardDetectChanged(PcmciaSlot& slot) {
    bool pulse;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        reg_card_status_change_ |= kCscDetectChange;
        pulse = (reg_status_change_int_cfg_ & kCfgCardDetectEnable) != 0u;
    }
    if (!slot.HasCard()) {
        /* Card removal drops the (level-triggered) card IRQ with it —
           the DeviceEmulator host runtime clears EINT8 on RemoveCard. */
        if (auto* line = emu_.TryGet<Pd6710CardIrqLine>()) line->Deassert();
    }
    if (pulse) {
        if (auto* line = emu_.TryGet<Pd6710ManagementIrqLine>()) line->Pulse();
    }
}

void Pd6710Controller::OnCardIrqAsserted(PcmciaSlot&) {
    bool enabled;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        enabled = (reg_interrupt_and_gen_ctrl_ & kCardIrqSelectMask) != 0u;
    }
    if (!enabled) return;
    if (auto* line = emu_.TryGet<Pd6710CardIrqLine>()) line->Assert();
}

void Pd6710Controller::OnCardIrqDeasserted(PcmciaSlot&) {
    bool enabled;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        enabled = (reg_interrupt_and_gen_ctrl_ & kCardIrqSelectMask) != 0u;
    }
    if (!enabled) return;
    if (auto* line = emu_.TryGet<Pd6710CardIrqLine>()) line->Deassert();
}

/* state_mutex_ is dropped before slot_.SaveState: the slot takes bus_mutex_,
   which ranks above state_mutex_ (see WriteIndexedDataLocked). */
void Pd6710Controller::SaveState(StateWriter& w) {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        w.Write(index_);
        w.Write(reg_power_control_); w.Write(reg_card_status_change_);
        w.Write(reg_status_change_int_cfg_); w.Write(reg_window_enable_);
        w.Write(reg_io_window_control_); w.Write(reg_interrupt_and_gen_ctrl_);
        w.Write(reg_chip_info_);
        w.WriteBytes(io_start_lo_, sizeof(io_start_lo_));
        w.WriteBytes(io_start_hi_, sizeof(io_start_hi_));
        w.WriteBytes(io_end_lo_, sizeof(io_end_lo_));
        w.WriteBytes(io_end_hi_, sizeof(io_end_hi_));
        w.WriteBytes(io_off_lo_, sizeof(io_off_lo_));
        w.WriteBytes(io_off_hi_, sizeof(io_off_hi_));
        w.WriteBytes(mem_reg_, sizeof(mem_reg_));
        w.WriteBytes(timing_, sizeof(timing_));
    }
    slot_.SaveState(w);
}

void Pd6710Controller::RestoreState(StateReader& r) {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        r.Read(index_);
        r.Read(reg_power_control_); r.Read(reg_card_status_change_);
        r.Read(reg_status_change_int_cfg_); r.Read(reg_window_enable_);
        r.Read(reg_io_window_control_); r.Read(reg_interrupt_and_gen_ctrl_);
        r.Read(reg_chip_info_);
        r.ReadBytes(io_start_lo_, sizeof(io_start_lo_));
        r.ReadBytes(io_start_hi_, sizeof(io_start_hi_));
        r.ReadBytes(io_end_lo_, sizeof(io_end_lo_));
        r.ReadBytes(io_end_hi_, sizeof(io_end_hi_));
        r.ReadBytes(io_off_lo_, sizeof(io_off_lo_));
        r.ReadBytes(io_off_hi_, sizeof(io_off_hi_));
        r.ReadBytes(mem_reg_, sizeof(mem_reg_));
        r.ReadBytes(timing_, sizeof(timing_));
    }
    slot_.RestoreState(r);
}

REGISTER_SERVICE(Pd6710Controller);
