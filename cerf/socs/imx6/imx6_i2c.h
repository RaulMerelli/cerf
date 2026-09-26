#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_i2c_bus.h"
#include "imx6_i2c_device.h"

#include <cstdint>

namespace cerf_imx6_i2c_detail {

template <uint32_t kBase> class Imx6I2c : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().RegisterResettable(this); }

    uint32_t MmioBase() const override { return kBase; }


private:
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadHalf(addr & ~1u) >> ((addr & 1u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
        /* IMX6DQRM Rev.2 §35.7.1: IADR "holds the address to which the I2C responds when addressed
           as a slave"; this model drives the bus as master, where the RM says it takes no part. */
        case 0x00u: return iadr_;
        /* IMX6DQRM Rev.2 §35.7.2: IFDR divides the source clock to produce SCL, a rate this
           untimed transfer model does not reproduce. */
        case 0x04u: return ifdr_;
        case 0x08u: return i2cr_;
        case 0x0Cu:
            CompleteTransmitByteOnStatusPoll();
            CompleteReceiveByteOnStatusPoll();
            i2sr_ = static_cast<uint16_t>((i2sr_ & (kIcf | kIif | kIal | kRxak)) | ((i2cr_ & kMsta) ? kIbb : 0u));
            return i2sr_;
        case 0x10u: return MasterReadData();
        }
        HaltUnsupportedAccess("ReadHalf", addr, 0);
    }
    uint32_t ReadWord(uint32_t addr) override { return ReadHalf(addr); }

    void WriteByte(uint32_t addr, uint8_t value) override {
        WriteHalf(addr & ~1u, static_cast<uint16_t>(value) << ((addr & 1u) * 8u));
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
        case 0x00u: iadr_ = value; return;
        case 0x04u: ifdr_ = value; return;
        case 0x08u: {
            const uint16_t old_cr = i2cr_;
            const bool was_master = (i2cr_ & kMsta) != 0;
            const bool is_master = (value & kMsta) != 0;
            const bool repeated = was_master && is_master && ((value & kRsta) != 0) && ((i2cr_ & kRsta) == 0);
            i2cr_ = value;
            if (!was_master && is_master) {
                i2sr_ |= kIbb;
                expecting_addr_ = true;
            } else if (repeated) {
                expecting_addr_ = true;
            } else if (was_master && !is_master) {
                i2sr_ = static_cast<uint16_t>((i2sr_ & ~kIbb) | kIcf | kIif);
                expecting_addr_ = true;
                rx_dummy_ = false;
                tx_complete_pending_ = false;
                if (device_ && read_phase_) {
                    stop_pending_final_read_ = true;
                } else {
                    device_ = nullptr;
                    read_phase_ = false;
                    stop_pending_final_read_ = false;
                }
            } else if (is_master && device_ && read_phase_ && ((old_cr & kMtx) != 0) && ((value & kMtx) == 0)) {
                rx_shift_ = 0x00u;
                rx_dummy_ = true;
            }
            return;
        }
        case 0x0Cu: {
            const bool had_iif = (i2sr_ & kIif) != 0;
            i2sr_ &= (value | ~(kIif | kIal));
            if (had_iif && (i2sr_ & kIif) == 0 && device_ && device_->TakePendingCompletion()) {
                tx_complete_pending_ = true;
            }
            return;
        }
        case 0x10u:
            i2dr_ = value;
            HandleWriteData(static_cast<uint8_t>(value));
            i2sr_ &= ~kIcf;
            tx_complete_pending_ = true;
            i2sr_ &= ~kRxak;
            return;
        }
        HaltUnsupportedAccess("WriteHalf", addr, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override { WriteHalf(addr, static_cast<uint16_t>(value)); }

    void SaveState(StateWriter& w) override {
        SaveControllerState(w);
        emu_.Get<Imx6I2cBus>().SaveDevices(kBase, w);
    }
    void RestoreState(StateReader& r) override {
        RestoreControllerState(r);
        emu_.Get<Imx6I2cBus>().RestoreDevices(kBase, r);
    }

    /* The bus restores its devices after the controller, so the slave the transfer was
       addressing is re-resolved once they are back. */
    void PostRestore() override {
        if (!expecting_addr_) device_ = emu_.Get<Imx6I2cBus>().Find(kBase, slave_addr_);
    }

    void SaveResetState(StateWriter& w) override { SaveControllerState(w); }
    void RestoreResetState(StateReader& r) override { RestoreControllerState(r); }

private:
    void SaveControllerState(StateWriter& w) const {
        w.Write(iadr_);
        w.Write(ifdr_);
        w.Write(i2cr_);
        w.Write(i2sr_);
        w.Write(i2dr_);
        w.Write(slave_addr_);
        w.Write(rx_shift_);
        w.Write<uint8_t>(static_cast<uint8_t>((expecting_addr_ ? 1u : 0u) | (read_phase_ ? 2u : 0u) |
                                              (rx_dummy_ ? 4u : 0u) | (stop_pending_final_read_ ? 8u : 0u) |
                                              (tx_complete_pending_ ? 16u : 0u)));
    }

    void RestoreControllerState(StateReader& r) {
        r.Read(iadr_);
        r.Read(ifdr_);
        r.Read(i2cr_);
        r.Read(i2sr_);
        r.Read(i2dr_);
        r.Read(slave_addr_);
        r.Read(rx_shift_);
        uint8_t flags = 0;
        r.Read(flags);
        expecting_addr_ = (flags & 1u) != 0u;
        read_phase_ = (flags & 2u) != 0u;
        rx_dummy_ = (flags & 4u) != 0u;
        stop_pending_final_read_ = (flags & 8u) != 0u;
        tx_complete_pending_ = (flags & 16u) != 0u;
        device_ = nullptr;
    }

    static constexpr uint16_t kMsta = 0x20u;
    static constexpr uint16_t kMtx = 0x10u;
    static constexpr uint16_t kIcf = 0x80u;
    static constexpr uint16_t kIbb = 0x20u;
    static constexpr uint16_t kIal = 0x10u;
    static constexpr uint16_t kIif = 0x02u;
    static constexpr uint16_t kRxak = 0x01u;
    static constexpr uint16_t kRsta = 0x04u;

    void HandleWriteData(uint8_t value) {
        if ((i2cr_ & kMtx) == 0) return;
        if (expecting_addr_) {
            slave_addr_ = static_cast<uint8_t>(value >> 1);
            read_phase_ = (value & 1u) != 0;
            device_ = emu_.Get<Imx6I2cBus>().Find(kBase, slave_addr_);
            if (!device_) FaultUnknownSlave();
            device_->StartTransfer(read_phase_);
            if (read_phase_) {
                rx_dummy_ = true;
                rx_shift_ = 0x00u;
            }
            expecting_addr_ = false;
            return;
        }
        if (device_ && !read_phase_) device_->WriteByte(value);
    }

    uint16_t MasterReadData() {
        i2sr_ |= kIcf;
        if (device_ && read_phase_ && ((i2cr_ & kMtx) == 0)) {
            const uint8_t out = rx_shift_;
            rx_shift_ = device_->ReadByte();
            rx_dummy_ = false;
            i2dr_ = out;
            i2sr_ |= kIif;
            if (stop_pending_final_read_) {
                device_ = nullptr;
                read_phase_ = false;
                stop_pending_final_read_ = false;
            }
            return i2dr_;
        }
        return i2dr_;
    }

    void CompleteReceiveByteOnStatusPoll() {
        if (!device_ || !read_phase_ || rx_dummy_) return;
        if ((i2cr_ & (kMsta | kMtx)) != kMsta) return;
        if ((i2sr_ & kIif) != 0) return;
        i2sr_ |= kIcf | kIif;
    }

    void CompleteTransmitByteOnStatusPoll() {
        if ((i2cr_ & kMsta) == 0) return;
        if ((i2sr_ & kIif) != 0) return;
        if (!tx_complete_pending_) return;
        i2sr_ |= kIcf | kIif;
        tx_complete_pending_ = false;
    }

    [[noreturn]] void FaultUnknownSlave() const {
        HaltUnsupportedAccess("imx6-i2c unknown slave", kBase + 0x10u, slave_addr_);
    }

    uint16_t iadr_ = 0, ifdr_ = 0, i2cr_ = 0, i2sr_ = 0, i2dr_ = 0;
    Imx6I2cDevice* device_ = nullptr;
    uint8_t slave_addr_ = 0, rx_shift_ = 0;
    bool expecting_addr_ = true, read_phase_ = false;
    bool rx_dummy_ = false;
    bool stop_pending_final_read_ = false;
    bool tx_complete_pending_ = false;
};

}

using cerf_imx6_i2c_detail::Imx6I2c;
