#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../boards/board_context.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_ecspi_endpoint.h"
#include "imx6_gic.h"

#include <cstdint>
#include <deque>

namespace cerf_imx6_ecspi_detail {

inline constexpr uint32_t kTestLoopBack = 0x80000000u;

template <uint32_t kBase> class Imx6Ecspi : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
        UpdateIrq();
    }

    uint32_t MmioBase() const override { return kBase; }


private:
    uint32_t MmioSize() const override { return 0x4000u; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - kBase) {
        case 0x00u: {
            if (rx_fifo_.empty())
                HaltUnsupportedAccess("imx6-ecspi RXDATA read with an empty receive FIFO", addr, 0);
            const uint32_t v = rx_fifo_.front();
            rx_fifo_.pop_front();
            RecomputeStatus();
            return v;
        }
        case 0x08u: return conreg_;
        case 0x0Cu: return configreg_;
        case 0x10u: return Enabled() ? intreg_ : 0u;
        case 0x14u: return Enabled() ? dmareg_ : 0u;
        case 0x18u: RecomputeStatus(); return statreg_;
        /* IMX6DQRM Rev.2 §21.7.9: TESTREG RXCNT[14:8] and TXCNT[6:0] report the number of words
           in each FIFO, and LBC[31] connects the transmitter to the receiver internally. */
        case 0x20u:
            return (static_cast<uint32_t>(rx_fifo_.size()) << 8) | static_cast<uint32_t>(tx_fifo_.size());
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - kBase) {
        case 0x04u:
            if (!Enabled())
                HaltUnsupportedAccess("imx6-ecspi TXDATA write with the block disabled", addr, value);
            if (tx_fifo_.size() >= kFifoDepth)
                HaltUnsupportedAccess("imx6-ecspi TXDATA write with a full transmit FIFO", addr, value);
            tx_fifo_.push_back(value);
            RecomputeStatus();
            return;
        case 0x08u:
            conreg_ = value;
            if (!(value & kConEn)) {
                tx_fifo_.clear();
                rx_fifo_.clear();
            } else if (value & kConXch) {
                DoExchange();
                conreg_ &= ~kConXch;
            }
            RecomputeStatus();
            return;
        case 0x0Cu: configreg_ = value; return;
        case 0x10u: intreg_ = value; UpdateIrq(); return;
        case 0x14u: dmareg_ = value; RecomputeStatus(); return;
        case 0x18u:
            statreg_ &= ~(value & (kStRo | kStTc));
            UpdateIrq();
            return;
        /* IMX6DQRM Rev.2 §21.7.8: PERIODREG sets the delay inserted between consecutive SPI
           bursts, which this untimed transfer model does not reproduce. */
        case 0x1Cu: return;
        case 0x20u:
            if ((value & kTestLoopBack) != 0u)
                HaltUnsupportedAccess("imx6-ecspi TESTREG loop back is not modelled", addr, value);
            return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write(conreg_);
        w.Write(configreg_);
        w.Write(intreg_);
        w.Write(dmareg_);
        w.Write(statreg_);
        w.Write(static_cast<uint32_t>(tx_fifo_.size()));
        for (uint32_t v : tx_fifo_)
            w.Write(v);
        w.Write(static_cast<uint32_t>(rx_fifo_.size()));
        for (uint32_t v : rx_fifo_)
            w.Write(v);
    }
    void RestoreState(StateReader& r) override {
        r.Read(conreg_);
        r.Read(configreg_);
        r.Read(intreg_);
        r.Read(dmareg_);
        r.Read(statreg_);
        tx_fifo_.clear();
        rx_fifo_.clear();
        uint32_t n = 0, v = 0;
        r.Read(n);
        for (uint32_t i = 0; i < n; ++i) {
            r.Read(v);
            tx_fifo_.push_back(v);
        }
        r.Read(n);
        for (uint32_t i = 0; i < n; ++i) {
            r.Read(v);
            rx_fifo_.push_back(v);
        }
    }
    void PostRestore() override {
        irq_level_ = false;
        RecomputeStatus();
    }

private:
    void DoExchange() {
        if (auto* endpoint = emu_.TryGet<Imx6EcspiEndpoint>();
            endpoint && endpoint->EcspiBase() == kBase) {
            tx_fifo_.clear();
            if (!endpoint->Exchange(conreg_, configreg_)) statreg_ |= kStRo;
            statreg_ |= kStTc;
            return;
        }
        if (!tx_fifo_.empty())
            emu_.Get<Fatal>().Die("i.MX6 ECSPI 0x%08X: exchange with no device wired to this instance",
                                  kBase);
        statreg_ |= kStTc;
    }
    void RecomputeStatus() {
        uint32_t s = statreg_ & (kStRo | kStTc);
        if (!Enabled()) {
            statreg_ = kStTe | kStTdr;
            UpdateIrq();
            return;
        }
        const uint32_t tx_threshold = dmareg_ & 0x3Fu;
        const uint32_t rx_threshold = (dmareg_ >> 16) & 0x3Fu;
        bool staged_transmit = false;
        if (const auto* endpoint = emu_.TryGet<Imx6EcspiEndpoint>();
            endpoint && endpoint->EcspiBase() == kBase)
            staged_transmit = endpoint->HasStagedTransmit();
        if (tx_fifo_.empty() && !staged_transmit) s |= kStTe;
        if (!staged_transmit && tx_fifo_.size() <= tx_threshold) s |= kStTdr;
        if (tx_fifo_.size() >= kFifoDepth) s |= kStTf;
        if (!rx_fifo_.empty()) s |= kStRr;
        if (rx_fifo_.size() > rx_threshold) s |= kStRdr;
        if (rx_fifo_.size() >= kFifoDepth) s |= kStRf;
        statreg_ = s;
        UpdateIrq();
    }

    void UpdateIrq() {
        const bool level = Enabled() && (statreg_ & intreg_ & 0xFFu) != 0u;
        if (level == irq_level_) return;
        irq_level_ = level;
        if (level)
            emu_.Get<Imx6Gic>().AssertSpi(IrqSpi());
        else
            emu_.Get<Imx6Gic>().DeAssertSpi(IrqSpi());
    }

    static constexpr uint32_t IrqSpi() {
        if constexpr (kBase == 0x02008000u)
            return 31u;
        else if constexpr (kBase == 0x0200C000u)
            return 32u;
        else if constexpr (kBase == 0x02010000u)
            return 33u;
        else if constexpr (kBase == 0x02014000u)
            return 34u;
        else
            return 35u;
    }

    bool Enabled() const { return (conreg_ & kConEn) != 0; }

    static constexpr uint32_t kConEn = 1u << 0;
    static constexpr uint32_t kConXch = 1u << 2;
    static constexpr uint32_t kStTe = 1u << 0;
    static constexpr uint32_t kStTdr = 1u << 1;
    static constexpr uint32_t kStTf = 1u << 2;
    static constexpr uint32_t kStRr = 1u << 3;
    static constexpr uint32_t kStRdr = 1u << 4;
    static constexpr uint32_t kStRf = 1u << 5;
    static constexpr uint32_t kStRo = 1u << 6;
    static constexpr uint32_t kStTc = 1u << 7;
    static constexpr size_t kFifoDepth = 64;

    uint32_t conreg_ = 0, configreg_ = 0, intreg_ = 0, dmareg_ = 0;
    uint32_t statreg_ = kStTe | kStTdr;
    bool irq_level_ = false;
    std::deque<uint32_t> tx_fifo_;
    std::deque<uint32_t> rx_fifo_;
};

}

using cerf_imx6_ecspi_detail::Imx6Ecspi;
