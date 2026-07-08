#include "cerf_virt_keyboard.h"
#include "cerf_virt_addr_map.h"

#include "../peripheral_dispatcher.h"
#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/keyboard_input.h"
#include "../../host/keyboard_router.h"
#include "../../state/state_stream.h"

REGISTER_SERVICE(CerfVirtKeyboard);

/* The channel page exists on every guest-additions board because the guest body
   maps it unconditionally; if it were board-gated, a non-SIMpad GA guest would
   map an unbacked PA and fault. Only the input routing (below) is SIMpad-only. */
bool CerfVirtKeyboard::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtKeyboard::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t CerfVirtKeyboard::MmioBase() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase() + CerfVirt::kKeyboardOffset;
}
uint32_t CerfVirtKeyboard::MmioSize() const { return CerfVirt::kKeyboardSize; }

uint32_t CerfVirtKeyboard::ReadWord(uint32_t addr) {
    uint32_t off = addr - MmioBase();
    if (off == CerfVirt::kKbWriteSeq) return write_seq_.load();
    if (off >= CerfVirt::kKbRingBase &&
        off < CerfVirt::kKbRingBase + CerfVirt::kKbRingCount * 4u) {
        return ring_[(off - CerfVirt::kKbRingBase) / 4u].load();
    }
    return 0u;
}

void CerfVirtKeyboard::WriteWord(uint32_t, uint32_t) {}

void CerfVirtKeyboard::PushKey(uint8_t vk, bool key_up) {
    uint32_t seq = write_seq_.load();
    uint32_t entry = (uint32_t)vk | (key_up ? CerfVirt::kKbEntryKeyUpBit : 0u);
    ring_[seq % CerfVirt::kKbRingCount].store(entry);
    /* Bump seq AFTER the entry store so the guest never reads a seq covering an
       index whose entry word hasn't landed yet. */
    write_seq_.store(seq + 1u);
}

void CerfVirtKeyboard::SaveState(StateWriter& w) {
    w.Write<uint32_t>(write_seq_.load());
    for (uint32_t i = 0; i < CerfVirt::kKbRingCount; ++i)
        w.Write<uint32_t>(ring_[i].load());
}

void CerfVirtKeyboard::RestoreState(StateReader& r) {
    uint32_t v;
    r.Read(v); write_seq_.store(v);
    for (uint32_t i = 0; i < CerfVirt::kKbRingCount; ++i) {
        r.Read(v); ring_[i].store(v);
    }
}

namespace {

/* The guest-additions keyboard source: routes host keystrokes through the
   channel to the guest's keybd_event. Available on every GA board and the
   default active source (highest priority) when guest additions is on. */
class CerfVirtKeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        emu_.Get<KeyboardRouter>().Register(this);
    }

    std::wstring   SourceName() const override { return L"Guest Additions keyboard"; }
    int            SourcePriority() const override { return 100; }
    const wchar_t* IconResourceName() const override { return L"ICON_GA_KEYBOARD"; }

    void OnHostKey(uint8_t vk, bool key_up) override {
        emu_.Get<CerfVirtKeyboard>().PushKey(vk, key_up);
    }
};

REGISTER_SERVICE(CerfVirtKeyboardInput);

}  /* namespace */
