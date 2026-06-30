#pragma once

#include "../peripherals/peripheral_base.h"

#include "../core/cerf_emulator.h"
#include "../boards/board_context.h"
#include "../peripherals/peripheral_dispatcher.h"
#include "../state/state_stream.h"

#include <cstdint>
#include <functional>

namespace cerf_freescale_gpio_detail {

constexpr uint32_t kGpioSize = 0x00004000u;

/* MCIMX51RM Table 35-2 / MCIMX31RM Table 5-3. EDGE_SEL (0x1C) is i.MX51-only. */
constexpr uint32_t kOffDr      = 0x00u;
constexpr uint32_t kOffGdir    = 0x04u;
constexpr uint32_t kOffPsr     = 0x08u;
constexpr uint32_t kOffIcr1    = 0x0Cu;
constexpr uint32_t kOffIcr2    = 0x10u;
constexpr uint32_t kOffImr     = 0x14u;
constexpr uint32_t kOffIsr     = 0x18u;
constexpr uint32_t kOffEdgeSel = 0x1Cu;

/* Freescale i.MX GPIO. The IP is shared across i.MX31 (MCIMX31RM Ch 5) and i.MX51
   (MCIMX51RM Ch 35) - same DR/GDIR/PSR/ICR1/ICR2/IMR/ISR - with i.MX51 adding
   EDGE_SEL (0x1C); registration is gated per concrete by kSoc. 32-bit access only
   (MCIMX51RM §35.3). */
template <uint32_t kBase, SocFamily kSoc>
class FreescaleGpioBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == kSoc;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kGpioSize; }

    /* A board device drives an external input pin level, observed by the guest
       through PSR (the pad-input semantic documented at the PSR read below).
       Used by the Ford iPod-auth coprocessor to hold its GPIO3.23 SOMI "ready"
       line high. Undriven pins stay 0. */
    void SetInputPin(uint32_t pin, bool level) {
        if (level) input_level_ |=  (1u << pin);
        else       input_level_ &= ~(1u << pin);
    }

    /* A board device that bit-bangs a protocol over GPIO pins (Ford SYNC2's
       software-I2C "HIC1:" on GPIO4.16/17, hsi2c.dll sub_C09F18D8) observes the
       guest's SCL/SDA edges through a write callback and reads the master-driven
       level via these getters; it drives the line back through SetInputPin. */
    void SetWriteObserver(std::function<void()> cb) { write_obs_ = std::move(cb); }
    bool PinIsOutput(uint32_t pin) const { return (gdir_ >> pin) & 1u; }
    bool PinOutLevel(uint32_t pin) const { return (dr_ >> pin) & 1u; }
    bool PinInputLevel(uint32_t pin) const { return (input_level_ >> pin) & 1u; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        if constexpr (kSoc == SocFamily::iMX51) {
            if (off == kOffEdgeSel) return edge_sel_;
        }
        switch (off) {
            /* A DR read returns the DR latch only for output bits; input bits
               (GDIR=0) return the pad value - MCIMX51RM §35.4.2.1 + p1041 NOTE
               ("while GDIR=0, a read access to DR does not return DR data"). A
               board device drives a pin's pad level via SetInputPin. */
            case kOffDr: {
                const uint32_t dr_val = (dr_ & gdir_) | (input_level_ & ~gdir_);
                return dr_val;
            }
            case kOffGdir: return gdir_;
            /* PSR always returns the pad input value (MCIMX31RM §5.3.3.3). */
            case kOffPsr:
                return input_level_;
            case kOffIcr1: return icr1_;
            case kOffIcr2: return icr2_;
            case kOffImr:  return imr_;
            case kOffIsr:  return isr_;
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kBase;
        if constexpr (kSoc == SocFamily::iMX51) {
            if (off == kOffEdgeSel) { edge_sel_ = value; return; }
        }
        switch (off) {
            case kOffDr:   dr_   = value; if (write_obs_) write_obs_(); return;
            case kOffGdir:
                gdir_ = value; if (write_obs_) write_obs_(); return;
            case kOffIcr1: icr1_ = value; return;
            case kOffIcr2: icr2_ = value; return;
            case kOffImr:  imr_  = value; return;
            case kOffIsr:  isr_ &= ~value; return;   /* ISR bits are w1c */
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write(dr_);   w.Write(gdir_); w.Write(icr1_);
        w.Write(icr2_); w.Write(imr_);  w.Write(isr_);
        w.Write(input_level_);
        if constexpr (kSoc == SocFamily::iMX51) w.Write(edge_sel_);
    }
    void RestoreState(StateReader& r) override {
        r.Read(dr_);   r.Read(gdir_); r.Read(icr1_);
        r.Read(icr2_); r.Read(imr_);  r.Read(isr_);
        r.Read(input_level_);
        if constexpr (kSoc == SocFamily::iMX51) r.Read(edge_sel_);
    }

private:
    uint32_t dr_       = 0;
    uint32_t gdir_     = 0;
    uint32_t icr1_     = 0;
    uint32_t icr2_     = 0;
    uint32_t imr_      = 0;
    uint32_t isr_      = 0;
    uint32_t edge_sel_ = 0;   /* i.MX51 only */
    uint32_t input_level_ = 0;  /* board-driven input pin levels read back via PSR */
    std::function<void()> write_obs_;  /* fired on DR/GDIR write (bit-bang observers) */
};

}  /* namespace cerf_freescale_gpio_detail */
