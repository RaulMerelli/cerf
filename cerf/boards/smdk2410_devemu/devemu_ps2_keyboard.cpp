#define NOMINMAX

#include "devemu_ps2_keyboard.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../host/keyboard_input.h"
#include "../../host/keyboard_map.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/irq_controller.h"
#include "../../state/state_stream.h"
#include "../board_detector.h"

REGISTER_SERVICE(DevEmuPs2Keyboard);

void DevEmuPs2Keyboard::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    w.Write(spcon_);
    w.Write(spsta_);
    w.Write(sppin_);
    w.Write(sppre_);
    w.Write(sptdat_);
    w.Write(sprdat_);
    w.WriteBytes(queue_, sizeof(queue_));
    w.Write<int32_t>(queue_head_);
    w.Write<int32_t>(queue_tail_);
    w.Write<uint8_t>(num_lock_state_ ? 1u : 0u);
}

void DevEmuPs2Keyboard::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    r.Read(spcon_);
    r.Read(spsta_);
    r.Read(sppin_);
    r.Read(sppre_);
    r.Read(sptdat_);
    r.Read(sprdat_);
    r.ReadBytes(queue_, sizeof(queue_));
    int32_t v;
    r.Read(v); queue_head_ = v;
    r.Read(v); queue_tail_ = v;
    uint8_t b;
    r.Read(b); num_lock_state_ = (b != 0);
}

namespace {

/* Register offsets relative to base 0x59000020. The six SPI register
   groups (SPCON / SPSTA / SPPIN / SPPRE / SPTDAT / SPRDAT) live at
   4-byte stride. */
constexpr uint32_t kRegSpcon  = 0x00u;
constexpr uint32_t kRegSpsta  = 0x04u;
constexpr uint32_t kRegSppin  = 0x08u;
constexpr uint32_t kRegSppre  = 0x0Cu;
constexpr uint32_t kRegSptdat = 0x10u;
constexpr uint32_t kRegSprdat = 0x14u;

/* SPSTA bit 0 — Transfer Ready Flag. Re-asserted on every SPTDAT
   write per the BSP's IOSPI1::WriteWord. */
constexpr uint32_t kSpstaRedy = 0x1u;

/* SMOD field in SPCON: bits 5:6 per the S3C2410A user manual chapter
   22 ("SPI"). The BSP keyboard driver enables interrupt mode by
   writing SMOD=1; in any other mode the keyboard ring is gated off
   (matches IOSPI1::EnqueueKey's "if (SPCON.Bits.SMOD != 1) return"). */
constexpr uint32_t kSpconSmodMask  = 0x60u;
constexpr uint32_t kSpconSmodShift = 5;
constexpr uint32_t kSpconSmodIntMode = 1;

/* IRQ_EINT1 = 1 from the BSP's s3c2410x_intr.h — the external
   interrupt line the BSP wires its virtual PS/2 keyboard controller
   to. Raised on every key enqueue (and re-raised inside SPRDAT-pop
   when more scancodes remain). */
constexpr int kIrqEint1 = 1;

/* PS/2 scancode make/break bit: OR into the byte to signal key-up. */
constexpr uint8_t kScKeyUp   = 0x80u;
constexpr uint8_t kScKeyDown = 0x00u;

/* PS/2 wire scancodes for the Fn-combo wrap + NumLock-off pre-sequence
   (kVkToScTable[VK 0xC0] / [VK 0xBD] in the BSP table). */
constexpr uint8_t kFnScancode     = 0x63u;
constexpr uint8_t kHyphenScancode = 0x51u;

/* device_code kinds packed by DevEmuKeyboardMap (bits 17:16). */
constexpr uint32_t kKindDirect    = 0u;
constexpr uint32_t kKindNumLock   = 2u;
constexpr uint8_t  kGuestVkHyphen = 0xBDu;

/* Adapter connecting HostWindow's KeyboardInput dispatch to the
   DevEmuPs2Keyboard peripheral. Anonymous-namespaced — its only role
   is to occupy the KeyboardInput slot and forward calls; nothing
   outside this .cpp names the type. */
class DevEmuKeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }
    void OnHostKey(uint8_t vk, bool key_up) override {
        emu_.Get<DevEmuPs2Keyboard>().OnHostKey(vk, key_up);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(DevEmuKeyboardInput, KeyboardInput);

bool DevEmuPs2Keyboard::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
}

void DevEmuPs2Keyboard::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t DevEmuPs2Keyboard::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    uint32_t value = 0;
    bool     keys_remain_after_pop = false;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        switch (off) {
            case kRegSpcon:
                value = spcon_;
                break;
            case kRegSpsta:
                value = spsta_;
                break;
            case kRegSppin:
                value = sppin_;
                break;
            case kRegSppre:
                value = sppre_;
                break;
            case kRegSptdat:
                value = sptdat_;
                break;
            case kRegSprdat:
                if (sptdat_ == 0xFF && queue_head_ != queue_tail_) {
                    sprdat_     = queue_[queue_tail_];
                    queue_tail_ = (queue_tail_ + 1) % kQueueLen;
                    keys_remain_after_pop = (queue_head_ != queue_tail_);
                } else {
                    sprdat_ = 0;
                }
                value = sprdat_;
                break;
            default:
                HaltUnsupportedAccess("ReadWord", addr, 0);  /* noreturn */
        }
    }
    /* Re-raise outside state_mutex_ — IrqController has its own lock
       and we don't want to hold ours across the call. */
    if (keys_remain_after_pop) {
        emu_.Get<IrqController>().AssertIrq(kIrqEint1);
    }
    return value;
}

void DevEmuPs2Keyboard::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    LOG(Periph, "[Ps2Kbd] write +0x%02X = 0x%08X\n", off, value);
    std::lock_guard<std::mutex> lk(state_mutex_);
    switch (off) {
        case kRegSpcon:
            spcon_ = value;
            break;
        case kRegSpsta:
            /* Read-only on the real SPI controller; drop the write. */
            break;
        case kRegSppin:
            sppin_ = value;
            break;
        case kRegSppre:
            sppre_ = value;
            break;
        case kRegSptdat:
            sptdat_ = static_cast<uint8_t>(value);
            spsta_ |= kSpstaRedy;
            break;
        case kRegSprdat:
            sprdat_ = static_cast<uint8_t>(value);
            break;
        default:
            HaltUnsupportedAccess("WriteWord", addr, value);  /* noreturn */
    }
}

void DevEmuPs2Keyboard::EnqueueScancode(uint8_t sc) {
    bool raise = false;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        const uint32_t smod = (spcon_ & kSpconSmodMask) >> kSpconSmodShift;
        if (smod != kSpconSmodIntMode) {
            /* Keyboard disabled by guest driver — drop the key on the
               floor, matching the BSP's IOSPI1::EnqueueKey behavior. */
            return;
        }
        queue_[queue_head_] = sc;
        queue_head_ = (queue_head_ + 1) % kQueueLen;
        /* No overflow check — head wrapping into tail silently
           overwrites the oldest pending scancode, also matching the
           BSP's behavior. */
        raise = true;
    }
    if (raise) {
        LOG(Periph, "[Ps2Kbd] enqueue 0x%02X, raising EINT1\n", sc);
        emu_.Get<IrqController>().AssertIrq(kIrqEint1);
    }
}

void DevEmuPs2Keyboard::OnHostKey(uint8_t vk, bool key_up) {
    uint32_t code;
    if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;

    const uint8_t  sc        = static_cast<uint8_t>(code & 0xFFu);
    const uint8_t  guest_vk  = static_cast<uint8_t>((code >> 8) & 0xFFu);
    const uint32_t kind      = (code >> 16) & 0x3u;
    const uint8_t  key_state = key_up ? kScKeyUp : kScKeyDown;

    /* Direct mapping — vast majority of keys land here. */
    if (kind == kKindDirect) {
        EnqueueScancode(static_cast<uint8_t>(sc | key_state));
        return;
    }

    /* NumLock-table key while NumLock is on: turn it off first with the
       "Fn + - down + - up + Fn" sequence (mirrors the BSP). */
    if (kind == kKindNumLock && num_lock_state_) {
        EnqueueScancode(static_cast<uint8_t>(kFnScancode     | kScKeyDown));
        EnqueueScancode(static_cast<uint8_t>(kHyphenScancode | kScKeyDown));
        EnqueueScancode(static_cast<uint8_t>(kHyphenScancode | kScKeyUp));
        EnqueueScancode(static_cast<uint8_t>(kFnScancode     | kScKeyUp));
        num_lock_state_ = false;
    }

    /* The Fn-mapped hyphen (host NumLock key) toggles the shadow NumLock state
       on key-down (BSP: pvkMapMatch == VK_HYPHEN && KeyUpDown == 0). */
    if (guest_vk == kGuestVkHyphen && !key_up)
        num_lock_state_ = !num_lock_state_;

    /* Wrapped sequence: Fn-down + mapped key + Fn-up. */
    EnqueueScancode(static_cast<uint8_t>(kFnScancode | kScKeyDown));
    EnqueueScancode(static_cast<uint8_t>(sc          | key_state));
    EnqueueScancode(static_cast<uint8_t>(kFnScancode | kScKeyUp));
}
