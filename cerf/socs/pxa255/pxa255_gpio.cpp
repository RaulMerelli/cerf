#include "pxa255_gpio.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "pxa255_intc.h"

bool Pxa255Gpio::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSoc() == SocFamily::PXA25x;
}

void Pxa255Gpio::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Pxa255Gpio::UpdateIntcLocked() {
    /* Table 4-35 ICPR: GPIO0 edge -> bit 8, GPIO1 edge -> bit 9, any
       GPIO[84:2] edge -> the collective bit 10 (GPIO 64-84 = bank2 bits 0-20). */
    uint32_t level = 0;
    if (gedr_[0] & 0x1u) level |= (1u << 8);
    if (gedr_[0] & 0x2u) level |= (1u << 9);
    const uint32_t coll = (gedr_[0] & ~0x3u) | gedr_[1] | (gedr_[2] & 0x001FFFFFu);
    if (coll) level |= (1u << 10);
    emu_.Get<Pxa255Intc>().SetSourceLevel((1u << 8) | (1u << 9) | (1u << 10), level);
}

void Pxa255Gpio::ApplyEdgesLocked(const uint32_t before[3]) {
    for (uint32_t b = 0; b < 3u; ++b) {
        const uint32_t now  = PinLevelLocked(b);
        const uint32_t rose = now & ~before[b] & grer_[b];   /* §4.1.3.4 rising edge. */
        const uint32_t fell = ~now & before[b] & gfer_[b];   /* §4.1.3.4 falling edge. */
        gedr_[b] |= (rose | fell);
    }
    UpdateIntcLocked();
}

void Pxa255Gpio::SetInputLevel(uint32_t gpio, bool high) {
    const uint32_t bank = gpio / 32u;
    if (bank >= 3u) return;
    const uint32_t bit = 1u << (gpio % 32u);
    std::lock_guard<std::mutex> g(mtx_);
    const uint32_t before[3] = { PinLevelLocked(0), PinLevelLocked(1), PinLevelLocked(2) };
    if (high) in_[bank] |= bit;
    else      in_[bank] &= ~bit;
    ApplyEdgesLocked(before);
}

uint32_t Pxa255Gpio::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if ((off & 3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    std::lock_guard<std::mutex> g(mtx_);
    if (off <= 0x08) {                                                /* GPLR (§4.1.3.1). */
        uint32_t v = PinLevelLocked(off / 4u);
        if (serial_slave_) v |= serial_slave_->DriveGplr(off / 4u);
        return v;
    }
    if (off >= 0x0C && off <= 0x14) return gpdr_[(off - 0x0C) / 4u];
    if (off >= 0x18 && off <= 0x2C) return 0u;                        /* GPSR/GPCR write-only. */
    if (off >= 0x30 && off <= 0x38) return grer_[(off - 0x30) / 4u];
    if (off >= 0x3C && off <= 0x44) return gfer_[(off - 0x3C) / 4u];
    if (off >= 0x48 && off <= 0x50) return gedr_[(off - 0x48) / 4u];  /* GEDR (§4.1.3.5). */
    if (off >= 0x54 && off <= 0x68) return gafr_[(off - 0x54) / 4u];
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Pxa255Gpio::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if ((off & 3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    std::lock_guard<std::mutex> g(mtx_);
    if (off <= 0x08) return;                                          /* GPLR read-only. */
    if (off >= 0x30 && off <= 0x38) { grer_[(off - 0x30) / 4u] = value; return; }
    if (off >= 0x3C && off <= 0x44) { gfer_[(off - 0x3C) / 4u] = value; return; }
    if (off >= 0x48 && off <= 0x50) {                                 /* GEDR W1C (§4.1.3.5). */
        gedr_[(off - 0x48) / 4u] &= ~value;
        UpdateIntcLocked();
        return;
    }
    if (off >= 0x54 && off <= 0x68) { gafr_[(off - 0x54) / 4u] = value; return; }
    if (off >= 0x0C && off <= 0x2C) {                                 /* GPDR/GPSR/GPCR move pin levels. */
        const uint32_t before[3] = { PinLevelLocked(0), PinLevelLocked(1),
                                     PinLevelLocked(2) };
        if (off <= 0x14)      gpdr_[(off - 0x0C) / 4u] = value;
        else if (off <= 0x20) out_[(off - 0x18) / 4u] |= value;       /* GPSR set. */
        else                  out_[(off - 0x24) / 4u] &= ~value;      /* GPCR clear. */
        ApplyEdgesLocked(before);
        if (serial_slave_) serial_slave_->OnGuestWrite(off, value);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Pxa255Gpio::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> g(mtx_);
    w.WriteBytes(in_,   sizeof(in_));
    w.WriteBytes(out_,  sizeof(out_));
    w.WriteBytes(gpdr_, sizeof(gpdr_));
    w.WriteBytes(grer_, sizeof(grer_));
    w.WriteBytes(gfer_, sizeof(gfer_));
    w.WriteBytes(gedr_, sizeof(gedr_));
    w.WriteBytes(gafr_, sizeof(gafr_));
    if (serial_slave_) serial_slave_->SaveState(w);
}

void Pxa255Gpio::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> g(mtx_);
    r.ReadBytes(in_,   sizeof(in_));
    r.ReadBytes(out_,  sizeof(out_));
    r.ReadBytes(gpdr_, sizeof(gpdr_));
    r.ReadBytes(grer_, sizeof(grer_));
    r.ReadBytes(gfer_, sizeof(gfer_));
    r.ReadBytes(gedr_, sizeof(gedr_));
    r.ReadBytes(gafr_, sizeof(gafr_));
    if (serial_slave_) serial_slave_->RestoreState(r);
}

REGISTER_SERVICE(Pxa255Gpio);
