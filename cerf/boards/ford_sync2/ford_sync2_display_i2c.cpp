#include "../../core/service.h"

#include "../../core/cerf_emulator.h"
#include "../board_detector.h"
#include "../../socs/imx51/imx51_gpio4.h"

#include <cstdint>

namespace {

/* Ford SYNC2 "HIC1:" is software-I2C bit-banged over GPIO4 (SCL=16, SDA=17;
   hsi2c.dll sub_C09F18D8), NOT the HS-I2C controller. This decodes the waveform
   (via GPIO4's write-observer) and ACKs the bit-bang bus so the master's
   transfers complete. */
constexpr uint32_t kScl = 16u;
constexpr uint32_t kSda = 17u;

/* Slave 7-bit address the hsi2c master clocks out, observed at runtime
   (address byte 0x90 -> 0x48). */
constexpr uint8_t kDmAddr = 0x48u;

class FordSync2DisplayI2c : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::FordSyncGen2;
    }
    void OnReady() override {
        gpio_ = &emu_.Get<Imx51Gpio4>();
        /* Both lines idle high (open-drain pull-up). SCL especially: the slave
           never clock-stretches, so the master's WaitForSCK readback (hsi2c
           sub_C09F16E4 releases GPIO4.16 then waits for it to read high) must
           read high - else it times out (0x102) every clock and the I2C crawls. */
        gpio_->SetInputPin(kScl, true);
        gpio_->SetInputPin(kSda, true);
        gpio_->SetWriteObserver([this] { OnEdge(); });
    }

private:
    void DriveSda(bool level) { gpio_->SetInputPin(kSda, level); }
    void ReleaseSda()         { DriveSda(true); }

    /* 0x48 is configured by register WRITES (runtime writes reg6 et al.),
       so it is NOT the DM's read-only type register; the DM type read targets an
       unidentified address. No DM is modelled -> return 0, never a fabricated type. */
    uint8_t ReadReg(uint8_t /*reg*/) const { return 0x00u; }

    void OnByteReceived(uint8_t b) {
        if (byte_idx_ == 0u) {                /* address byte */
            addr_ = b >> 1; rw_ = b & 1u;
            addressed_ = (addr_ == kDmAddr);
        } else {                              /* write byte = register pointer */
            reg_ = b;
        }
    }

    void OnEdge() {
        /* Open-drain wired-AND: a line is high only if released (input or
           output-high) AND nothing pulls it low; the slave's ACK pull is in the
           same input_level_ the master reads back, so both lines read symmetrically. */
        const bool scl = (!gpio_->PinIsOutput(kScl) || gpio_->PinOutLevel(kScl)) &&
                         gpio_->PinInputLevel(kScl);
        const bool sda = (!gpio_->PinIsOutput(kSda) || gpio_->PinOutLevel(kSda)) &&
                         gpio_->PinInputLevel(kSda);

        if (scl && prev_scl_) {                       /* SDA edge while SCL high */
            if (prev_sda_ && !sda) {                  /* (repeated) START - reg_ kept */
                active_ = true; bit_ = 0u; shift_ = 0u;
                tx_this_ = false; byte_idx_ = 0u; addressed_ = false;
                ReleaseSda();
            } else if (!prev_sda_ && sda) {           /* STOP */
                active_ = false; ReleaseSda();
            }
        } else if (active_) {
            if (prev_scl_ && !scl) {                  /* SCL falling - set up next bit */
                if (bit_ < 8u) {
                    if (tx_this_ && addressed_) DriveSda((tx_byte_ >> (7u - bit_)) & 1u);
                    else ReleaseSda();
                } else {                              /* ACK bit */
                    if (!tx_this_ && addressed_) DriveSda(false);  /* slave ACKs RX byte */
                    else ReleaseSda();                /* master ACKs our TX byte */
                }
            } else if (!prev_scl_ && scl) {           /* SCL rising - sample */
                if (bit_ < 8u) {
                    if (!tx_this_) shift_ = (shift_ << 1) | (sda ? 1u : 0u);
                    if (++bit_ == 8u && !tx_this_)    /* RX byte done BEFORE its ACK */
                        OnByteReceived(static_cast<uint8_t>(shift_));
                } else {                              /* ACK clock */
                    if (tx_this_) master_nack_ = sda;
                    bit_ = 0u; shift_ = 0u; ++byte_idx_;
                    tx_this_ = addressed_ && rw_ && !master_nack_;  /* read -> TX next byte */
                    if (tx_this_) { tx_byte_ = ReadReg(reg_); ++reg_; }
                }
            }
        }
        prev_scl_ = scl; prev_sda_ = sda;
    }

    Imx51Gpio4* gpio_ = nullptr;
    bool prev_scl_ = true;
    bool prev_sda_ = true;
    bool active_ = false;
    bool tx_this_ = false;    /* current byte is slave-transmitted (read data) */
    bool addressed_ = false;
    bool rw_ = false;
    bool master_nack_ = false;
    uint32_t bit_ = 0;
    uint32_t shift_ = 0;
    uint8_t  byte_idx_ = 0;
    uint8_t  addr_ = 0;
    uint8_t  reg_ = 0;
    uint8_t  tx_byte_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(FordSync2DisplayI2c);
