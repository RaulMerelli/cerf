#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../host/host_gdiplus.h"
#include "../../host/host_widget.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace cerf_vr41xx_led_detail {

/* NEC VR41xx LED Control Unit (VR4102 UM ch.23 == VR4131 UM ch.19). */
constexpr uint32_t kOffHts  = 0x00u;   /* LEDHTSREG  (VR4131 UM 19.2.1) */
constexpr uint32_t kOffLts  = 0x02u;   /* LEDLTSREG  (VR4131 UM 19.2.2) */
constexpr uint32_t kOffCnt  = 0x08u;   /* LEDCNTREG  (VR4131 UM 19.2.3) */
constexpr uint32_t kOffAstc = 0x0Au;   /* LEDASTCREG (VR4131 UM 19.2.4) */
constexpr uint32_t kOffInt  = 0x0Cu;   /* LEDINTREG  (VR4131 UM 19.2.5) */

/* LEDCNTREG: D1 LEDSTOP | D0 LEDENABLE R/W, D15:2 RFU; RTCRST = LEDSTOP(1)/
   LEDENABLE(0), other-resets retained (VR4131 UM 19.2.3). */
constexpr uint16_t kCntMask    = 0x0003u;
constexpr uint16_t kCntEnable  = 0x0001u;
constexpr uint16_t kCntPowerOn = 0x0002u;

/* LEDHTSREG on-time / LEDLTSREG off-time in 0.0625 s units (VR4131 UM 19.2.1/19.2.2). */
const COLORREF kClrLit      = RGB(78, 201, 90);
const COLORREF kClrDark     = RGB(50, 55, 50);
const COLORREF kClrRim      = RGB(90, 95, 90);
const COLORREF kClrRimBlink = RGB(120, 230, 130);
constexpr uint32_t kFallbackBlinkTicks = 5u;

template <SocFamily Soc, uint32_t Base>
class Vr41xxLedBase : public Peripheral, public HostWidget {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == Soc;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<HostWidgetRegistry>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc) {
                std::lock_guard<std::mutex> lk(state_mutex_);
                cnt_ = kCntPowerOn;
            }
        });
    }

    uint32_t MmioBase() const override { return Base; }
    uint32_t MmioSize() const override { return 0x10u; }

    uint16_t ReadHalf(uint32_t addr) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        switch (addr - Base) {
            case kOffCnt: return cnt_;
            /* LEDINTREG (VR4131 UM 19.2.5); gwes.exe sub_74CBC @0x74D0E reads it
               as a discarded write-flush. */
            case kOffInt: return 0u;
            default: HaltUnsupportedAccess("LED ReadHalf", addr, 0);
        }
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        switch (addr - Base) {
            case kOffHts:  hts_ = value; return;
            case kOffLts:  lts_ = value; return;
            case kOffCnt:  cnt_ = static_cast<uint16_t>(value & kCntMask); return;
            case kOffAstc: return;
            default: WriteHalfExt(addr, value); return;
        }
    }
    /* The CE OAL drives LEDCNTREG/LEDASTCREG with byte stores (nk.exe 0xBF033BC0
       "sb -> 0x0F000188"); NetBSD vrled.c:80 uses halfword - the unit takes both. */
    void WriteByte(uint32_t addr, uint8_t value) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        switch (addr - Base) {
            case kOffCnt:  cnt_ = static_cast<uint16_t>(value & kCntMask); return;
            case kOffAstc: return;
            default: HaltUnsupportedAccess("LED WriteByte", addr, value);
        }
    }
    /* gwes.exe sub_74CBC halfword-writes HTS/LTS separately; a 32-bit store to
       LEDHTSREG covers LEDHTSREG (low 16) + LEDLTSREG (high 16). */
    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - Base == kOffHts) {
            std::lock_guard<std::mutex> lk(state_mutex_);
            hts_ = static_cast<uint16_t>(value & 0xFFFFu);
            lts_ = static_cast<uint16_t>(value >> 16);
            return;
        }
        HaltUnsupportedAccess("LED WriteWord", addr, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("LED ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("LED ReadWord", addr, 0); }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        w.Write(cnt_); w.Write(hts_); w.Write(lts_);
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        r.Read(cnt_); r.Read(hts_); r.Read(lts_);
    }

    std::wstring WidgetName() const override { return L"Notification LED"; }
    WidgetGroup  Group() const override { return WidgetGroup::Indicator; }
    std::wstring Tooltip() const override {
        uint16_t cnt, hts, lts;
        { std::lock_guard<std::mutex> lk(state_mutex_); cnt = cnt_; hts = hts_; lts = lts_; }
        wchar_t buf[96];
        if (cnt & kCntEnable)
            swprintf_s(buf, L"Notification LED: blinking (on %ums / off %ums)",
                       static_cast<unsigned>(hts) * 125u / 2u,
                       static_cast<unsigned>(lts) * 125u / 2u);
        else
            swprintf_s(buf, L"Notification LED: off");
        return buf;
    }
    void DrawIcon(HDC dc, const RECT& box) const override {
        const int cx = (box.left + box.right) / 2;
        const int cy = (box.top + box.bottom) / 2;
        constexpr int kR = 5;
        emu_.Get<HostGdiPlus>().FillCircleAA(dc, cx, cy, kR,
            draw_lit_ ? kClrLit : kClrDark,
            draw_blink_ ? kClrRimBlink : kClrRim);
    }
    bool PollDirty() override {
        uint16_t cnt, hts, lts;
        { std::lock_guard<std::mutex> lk(state_mutex_); cnt = cnt_; hts = hts_; lts = lts_; }
        ++tick_;
        const bool blink = (cnt & kCntEnable) != 0u;
        bool lit = false;
        if (blink) {
            uint32_t on_ticks  = (static_cast<uint32_t>(hts) * 5u + 4u) / 8u;
            uint32_t off_ticks = (static_cast<uint32_t>(lts) * 5u + 4u) / 8u;
            if (on_ticks == 0u && off_ticks == 0u)
                on_ticks = off_ticks = kFallbackBlinkTicks;
            const uint32_t cyc = on_ticks + off_ticks;
            lit = cyc == 0u ? true : (tick_ % cyc) < on_ticks;
        }
        if (lit == draw_lit_ && blink == draw_blink_) return false;
        draw_lit_   = lit;
        draw_blink_ = blink;
        return true;
    }

protected:
    virtual void WriteHalfExt(uint32_t addr, uint16_t value) {
        HaltUnsupportedAccess("LED WriteHalf", addr, value);
    }

private:
    mutable std::mutex state_mutex_;
    uint16_t cnt_ = kCntPowerOn;
    uint16_t hts_ = 0;
    uint16_t lts_ = 0;

    uint32_t tick_       = 0;
    bool     draw_lit_   = false;
    bool     draw_blink_ = false;
};

}  /* namespace cerf_vr41xx_led_detail */
