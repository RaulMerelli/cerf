#pragma once

#include "../../peripherals/peripheral_base.h"
#include "../../state/state_stream.h"

#include <cstdint>
#include <mutex>

/* SPI slave attached to one McSPI channel. The McSPI controller
   forwards each TX-register write to the slave's Transfer and
   stores the returned value into RX. wl_bits is the effective
   word length programmed via CHCONF.WL (1..32). */
class McspiSlave {
public:
    virtual ~McspiSlave() = default;
    virtual uint32_t Transfer(uint32_t tx_word, uint32_t wl_bits) = 0;
};

class Omap3530Mcspi1 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x48098000u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void RegisterSlave(uint32_t channel, McspiSlave* slave);

    /* RegisterSlave is a cross-thread caller, so the same mutex guards
       state in both methods. The McspiSlave* pointers are host wiring,
       re-established by RegisterSlave at construction - not serialized. */
    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(mu_);
        w.Write(sysconfig_);
        w.Write(irqstatus_);
        w.Write(irqenable_);
        w.Write(wakeupenable_);
        w.Write(syst_);
        w.Write(modulctrl_);
        for (const Channel& c : channels_) {
            w.Write(c.chconf);
            w.Write(c.chctrl);
            w.Write(c.rx);
            w.Write(c.rx_full);
        }
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mu_);
        r.Read(sysconfig_);
        r.Read(irqstatus_);
        r.Read(irqenable_);
        r.Read(wakeupenable_);
        r.Read(syst_);
        r.Read(modulctrl_);
        for (Channel& c : channels_) {
            r.Read(c.chconf);
            r.Read(c.chctrl);
            r.Read(c.rx);
            r.Read(c.rx_full);
        }
    }

private:
    struct Channel {
        uint32_t    chconf  = 0;
        uint32_t    chctrl  = 0;
        uint32_t    rx      = 0;
        bool        rx_full = false;
        McspiSlave* slave   = nullptr;
    };

    void     PerformTransfer(uint32_t channel_index, uint32_t tx_word);
    uint32_t WordLength(const Channel& c) const;

    static constexpr uint32_t kNumChannels = 4;

    mutable std::mutex mu_;
    uint32_t sysconfig_    = 0;
    uint32_t irqstatus_    = 0;
    uint32_t irqenable_    = 0;
    uint32_t wakeupenable_ = 0;
    uint32_t syst_         = 0;
    uint32_t modulctrl_    = 0;
    Channel  channels_[kNumChannels];
};
