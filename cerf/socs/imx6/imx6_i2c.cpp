#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_i2c_bus.h"
#include "imx6_i2c_device.h"

#include <cstdint>

namespace {

/* i.MX6 I2C master controller (Freescale 16-bit register block, as on
   i.MX31/i.MX51). Emulates the bus and the master state machine and delegates
   the addressed slave's byte exchange to an Imx6I2cDevice resolved through
   Imx6I2cBus. Unknown slaves fault loud so missing board hardware is visible. */
template <uint32_t kBase>
class Imx6I2c : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadHalf(addr & ~1u) >> ((addr & 1u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
            case 0x00u: return iadr_;
            case 0x04u: return ifdr_;
            case 0x08u: return i2cr_;
            case 0x0Cu:
                /* The RTC driver busy-polls I2SR with short timeouts. Report
                   transfer-complete/ACK, but only raise IIF once an I2DR byte
                   has actually completed: IIF during START makes the driver
                   abort before writing the address byte. */
                CompleteTransmitByteOnStatusPoll();
                CompleteReceiveByteOnStatusPoll();
                i2sr_ = static_cast<uint16_t>(
                    (i2sr_ & (kIcf | kIif | kIal | kRxak)) |
                    ((i2cr_ & kMsta) ? kIbb : 0u));
                return i2sr_;
            case 0x10u:
                return MasterReadData();
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
                const bool is_master  = (value & kMsta) != 0;
                const bool repeated   = was_master && is_master &&
                                        ((value & kRsta) != 0) &&
                                        ((i2cr_ & kRsta) == 0);
                i2cr_ = value;
                if (!was_master && is_master) {
                    i2sr_ |= kIbb;
                    expecting_addr_ = true;
                } else if (repeated) {
                    expecting_addr_ = true;
                } else if (was_master && !is_master) {
                    /* STOP completes the outstanding cycle: bus-not-busy plus
                       IIF, which several WinCE paths poll after clearing MSTA. */
                    i2sr_ = static_cast<uint16_t>((i2sr_ & ~kIbb) | kIcf | kIif);
                    expecting_addr_ = true;
                    rx_dummy_ = false;
                    tx_complete_pending_ = false;
                    /* i.MX receive issues STOP before reading the final byte
                       from I2DR; keep the slave selected until that read. */
                    if (device_ && read_phase_) {
                        stop_pending_final_read_ = true;
                    } else {
                        device_ = nullptr;
                        read_phase_ = false;
                        stop_pending_final_read_ = false;
                    }
                } else if (is_master && device_ && read_phase_ &&
                           ((old_cr & kMtx) != 0) && ((value & kMtx) == 0)) {
                    /* i.MX master-receive is delayed one byte: the first I2DR
                       read after the address phase is a dummy read that clocks
                       the first real byte into the shift register. */
                    rx_shift_ = 0x00u;
                    rx_dummy_ = true;
                }
                return;
            }
            case 0x0Cu: {
                /* IIF/IAL are write-0-to-clear. */
                const bool had_iif = (i2sr_ & kIif) != 0;
                i2sr_ &= (value | ~(kIif | kIal));
                if (had_iif && (i2sr_ & kIif) == 0 && device_ &&
                    device_->TakePendingCompletion()) {
                    /* The addressed slave asked for a second byte-completion
                       after its just-written command byte. */
                    tx_complete_pending_ = true;
                }
                return;
            }
            case 0x10u:
                i2dr_ = value;
                HandleWriteData(static_cast<uint8_t>(value));
                i2sr_ &= ~kIcf;
                tx_complete_pending_ = true;
                i2sr_ &= ~kRxak;  /* ACK */
                return;
        }
        HaltUnsupportedAccess("WriteHalf", addr, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        WriteHalf(addr, static_cast<uint16_t>(value));
    }

    void SaveState(StateWriter& w) override {
        w.Write(iadr_); w.Write(ifdr_); w.Write(i2cr_);
        w.Write(i2sr_); w.Write(i2dr_);
        emu_.Get<Imx6I2cBus>().SaveDevices(kBase, w);
    }
    void RestoreState(StateReader& r) override {
        r.Read(iadr_); r.Read(ifdr_); r.Read(i2cr_);
        r.Read(i2sr_); r.Read(i2dr_);
        device_ = nullptr;
        expecting_addr_ = true;
        read_phase_ = false;
        rx_dummy_ = false;
        stop_pending_final_read_ = false;
        tx_complete_pending_ = false;
        emu_.Get<Imx6I2cBus>().RestoreDevices(kBase, r);
    }

private:
    static constexpr uint16_t kMsta = 0x20u;
    static constexpr uint16_t kMtx  = 0x10u;
    static constexpr uint16_t kIcf  = 0x80u;
    static constexpr uint16_t kIbb  = 0x20u;
    static constexpr uint16_t kIal  = 0x10u;
    static constexpr uint16_t kIif  = 0x02u;
    static constexpr uint16_t kRxak = 0x01u;
    static constexpr uint16_t kRsta = 0x04u;

    void HandleWriteData(uint8_t value) {
        if ((i2cr_ & kMtx) == 0) return;  /* dummy write before RX read */
        if (expecting_addr_) {
            slave_addr_ = static_cast<uint8_t>(value >> 1);
            read_phase_ = (value & 1u) != 0;
            device_ = emu_.Get<Imx6I2cBus>().Find(kBase, slave_addr_);
            if (!device_) {
                i2sr_ |= kRxak;
                FaultUnknownSlave();
            }
            device_->StartTransfer(read_phase_);
            if (read_phase_) {
                rx_dummy_ = true;
                rx_shift_ = 0x00u;
            }
            expecting_addr_ = false;
            return;
        }
        if (device_ && !read_phase_)
            device_->WriteByte(value);
    }

    uint16_t MasterReadData() {
        i2sr_ |= kIcf;
        if (device_ && read_phase_ && ((i2cr_ & kMtx) == 0)) {
            const uint8_t out = rx_shift_;
            rx_shift_ = device_->ReadByte();
            rx_dummy_ = false;
            i2dr_ = out;
            i2sr_ |= kIif;  /* the next byte has now completed */
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
        /* After the dummy I2DR read clocks the first byte, subsequent bytes
           complete autonomously on SCL and assert IIF. */
        i2sr_ |= kIcf | kIif;
    }

    void CompleteTransmitByteOnStatusPoll() {
        if ((i2cr_ & kMsta) == 0) return;
        if ((i2sr_ & kIif) != 0) return;
        if (!tx_complete_pending_) return;
        /* The byte completes on SCL after the I2DR write; drivers clear IIF
           then poll for completion. One completion per write: synthesizing
           more turns the pointer-write phase into a false-completion loop. */
        i2sr_ |= kIcf | kIif;
        tx_complete_pending_ = false;
    }

    [[noreturn]] void FaultUnknownSlave() const {
        HaltUnsupportedAccess("imx6-i2c unknown slave", kBase + 0x10u, slave_addr_);
    }

    uint16_t iadr_ = 0, ifdr_ = 0, i2cr_ = 0, i2sr_ = 0, i2dr_ = 0;
    Imx6I2cDevice* device_ = nullptr;
    uint8_t  slave_addr_ = 0, rx_shift_ = 0;
    bool     expecting_addr_ = true, read_phase_ = false;
    bool     rx_dummy_ = false;
    bool     stop_pending_final_read_ = false;
    bool     tx_complete_pending_ = false;
};

class Imx6I2c1 : public Imx6I2c<0x021A0000u> { using Imx6I2c::Imx6I2c; };
class Imx6I2c2 : public Imx6I2c<0x021A4000u> { using Imx6I2c::Imx6I2c; };
class Imx6I2c3 : public Imx6I2c<0x021A8000u> { using Imx6I2c::Imx6I2c; };

}  /* namespace */

REGISTER_SERVICE(Imx6I2c1);
REGISTER_SERVICE(Imx6I2c2);
REGISTER_SERVICE(Imx6I2c3);

