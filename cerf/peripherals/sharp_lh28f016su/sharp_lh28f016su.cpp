#include "../peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../boot/rom_parser_service.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../state/state_stream.h"
#include "../peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* Sharp LH28F016SU, 16M (1M x 16, 2M x 8) flash memory, LH28F008SA-compatible
   command set. Two devices sit in parallel on the PR31700's 32-bit CS0 port, so
   every command and every identifier code appears in both 16-bit halves. */

/* Word-wide bus operations (datasheet PDF p.8): A1 low reads the manufacturer ID
   00B0H, A1 high the device ID 6688H. The board crosses the byte lanes, so the CPU
   sees each halfword swapped - nk.exe 0x9F4321B0-0x9F4321C8 builds the compares
   against the swapped codes, and the CS2 buffer crosses the same lanes. */
constexpr uint32_t kIdentWordMfr = 0xB000B000u;
constexpr uint32_t kIdentWordDev = 0x88668866u;

/* Command bus definitions (datasheet PDF p.9). */
constexpr uint8_t kCmdReadArray  = 0xFFu;
constexpr uint8_t kCmdIdentifier = 0x90u;

constexpr uint32_t kIdentOffsetMfr = 0u;
constexpr uint32_t kIdentOffsetDev = 4u;

constexpr uint32_t kKsegUnmask = 0x1FFFFFFFu;

class SharpLh28F016Su : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsNino300;
    }

    void OnReady() override {
        auto& rom = emu_.Get<RomParserService>();
        if (!rom.Ok() || rom.Loaded().empty() || rom.Primary().xips.empty()) {
            LOG(Caution, "SharpLh28F016Su: ROM not parsed\n");
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        const ParsedROMHDR& hdr = rom.Primary().xips.front().toc.romhdr;
        base_ = hdr.physfirst & kKsegUnmask;
        size_ = hdr.physlast - hdr.physfirst;
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return base_; }
    uint32_t MmioSize() const override { return size_; }

    /* Reads of the array never reach here: CS0 is backed PAGE_EXECUTE_READ, so a
       read resolves to that host memory. The identifier codes are therefore
       presented by mutating the backing, the way Intel28F128J3::PresentId does. */
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint16_t lo = static_cast<uint16_t>(value);
        const uint16_t hi = static_cast<uint16_t>(value >> 16);
        if (lo != hi) {
            HaltUnsupportedAccess("Sharp LH28F016SU per-device command", addr, value);
        }
        /* The crossed byte lanes put the device's DQ0-DQ7 in the halfword's
           high byte. */
        switch (static_cast<uint8_t>(lo >> 8)) {
            case kCmdIdentifier: PresentIdent(); return;
            case kCmdReadArray:  RestoreArray(); return;
            default:
                HaltUnsupportedAccess("Sharp LH28F016SU command", addr, value);
        }
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint8_t>(ident_presented_ ? 1u : 0u);
        w.Write(array_word_mfr_);
        w.Write(array_word_dev_);
    }

    void RestoreState(StateReader& r) override {
        uint8_t presented = 0;
        r.Read(presented);
        r.Read(array_word_mfr_);
        r.Read(array_word_dev_);
        ident_presented_ = presented != 0u;
    }

private:
    void PresentIdent() {
        auto& mem = emu_.Get<EmulatedMemory>();
        if (!ident_presented_) {
            array_word_mfr_  = mem.ReadWord(base_ + kIdentOffsetMfr);
            array_word_dev_  = mem.ReadWord(base_ + kIdentOffsetDev);
            ident_presented_ = true;
        }
        mem.WriteWord(base_ + kIdentOffsetMfr, kIdentWordMfr);
        mem.WriteWord(base_ + kIdentOffsetDev, kIdentWordDev);
    }

    void RestoreArray() {
        if (!ident_presented_) return;
        auto& mem = emu_.Get<EmulatedMemory>();
        mem.WriteWord(base_ + kIdentOffsetMfr, array_word_mfr_);
        mem.WriteWord(base_ + kIdentOffsetDev, array_word_dev_);
        ident_presented_ = false;
    }

    uint32_t base_ = 0;
    uint32_t size_ = 0;

    bool     ident_presented_ = false;
    uint32_t array_word_mfr_  = 0;
    uint32_t array_word_dev_  = 0;
};

}  /* namespace */

REGISTER_SERVICE(SharpLh28F016Su);
