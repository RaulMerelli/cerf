#include "intel_28f128j3.h"

#include "../../cpu/emulated_memory.h"
#include "../peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstring>

/* Per-chip CFI query table, byte-exact from the Intel 28F128J3 datasheet
   (Intel order 290667) Appendix B Tables 24-27, indexed from word offset
   kCfiFirst (0x10): "QRY", Intel command set 0x0001, 16 MB device (0x27=0x18),
   one 128 KB x 128-block erase region (0x2D=0x7F .. 0x30=0x02), "PRI" 1.1. */
const uint8_t Intel28F128J3::kCfi[] = {
    /* 0x10 */ 0x51, 0x52, 0x59,             /* "QRY"                          */
    /* 0x13 */ 0x01, 0x00,                   /* primary command set (Intel)    */
    /* 0x15 */ 0x31, 0x00,                   /* primary ext query table @ 0x31 */
    /* 0x17 */ 0x00, 0x00,                   /* no alternate command set       */
    /* 0x19 */ 0x00, 0x00,                   /* no alternate ext query table   */
    /* 0x1B */ 0x27, 0x36, 0x00, 0x00,       /* Vcc 2.7-3.6 V, no Vpp          */
    /* 0x1F */ 0x07, 0x07, 0x0A, 0x00,       /* typ word/buf/block/chip times  */
    /* 0x23 */ 0x04, 0x04, 0x04, 0x00,       /* max word/buf/block/chip times  */
    /* 0x27 */ 0x18,                         /* device size 2^24 = 16 MB       */
    /* 0x28 */ 0x02, 0x00,                   /* x8/x16 async interface         */
    /* 0x2A */ 0x05, 0x00,                   /* max write buffer 2^5 = 32 B    */
    /* 0x2C */ 0x01,                         /* one erase block region         */
    /* 0x2D */ 0x7F, 0x00,                   /* 128 blocks (count-1 = 0x7F)    */
    /* 0x2F */ 0x00, 0x02,                   /* block size 0x200 * 256 = 128 KB*/
    /* 0x31 */ 0x50, 0x52, 0x49,             /* "PRI"                          */
    /* 0x34 */ 0x31, 0x31,                   /* extended query version 1.1     */
    /* 0x36 */ 0x0A, 0x00, 0x00, 0x00,       /* optional feature support       */
    /* 0x3A */ 0x01,                         /* functions after suspend        */
    /* 0x3B */ 0x01, 0x00,                   /* block status register mask     */
    /* 0x3D */ 0x33, 0x00,                   /* Vcc/Vpp optimum 3.3 V / none   */
};

/* mode_ = CFI command-FSM latch; shadow_ = ID/CFI/lock-presentation undo log
   (the backing is mutated to present those, captured by the Flash section, and
   undone on read-array - so the undo log must survive a mid-presentation save);
   block_locked_ = persistent per-block lock bits (datasheet Table 5). */
void Intel28F128J3::SaveState(StateWriter& w) {
    w.Write(mode_);
    w.Write<uint64_t>(shadow_.size());
    for (const auto& pr : shadow_) { w.Write(pr.first); w.Write(pr.second); }
    w.Write<uint64_t>(block_locked_.size());
    if (!block_locked_.empty())
        w.WriteBytes(block_locked_.data(), block_locked_.size());
}

void Intel28F128J3::RestoreState(StateReader& r) {
    r.Read(mode_);
    uint64_t n = 0; r.Read(n);
    shadow_.clear();
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t a = 0; uint8_t b = 0; r.Read(a); r.Read(b);
        shadow_.push_back({a, b});
    }
    uint64_t m = 0; r.Read(m);
    block_locked_.assign(static_cast<size_t>(m), 0u);
    if (m) r.ReadBytes(block_locked_.data(), static_cast<size_t>(m));
}

void Intel28F128J3::OnReady() {
    (void)emu_.Get<EmulatedMemory>().Translate(MmioBase());   /* ensure backing */
    block_locked_.assign(NumBlocks(), 0u);                    /* power-up: all blocks unlocked */
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t* Intel28F128J3::Host(uint32_t addr) {
    return emu_.Get<EmulatedMemory>().TryTranslate(addr);
}

/* Save the original backing byte at `addr`, then overwrite it with `v`. Saved
   pairs accumulate in shadow_ and are replayed in reverse on read-array. */
void Intel28F128J3::SaveAndWrite(uint32_t addr, uint8_t v) {
    uint8_t* p = Host(addr);
    if (!p) return;
    shadow_.push_back({addr, *p});
    *p = v;
}

/* Write a single CFI/ID/status byte into each chip lane at bus address `addr`:
   the chip's low byte carries `v`, its upper bytes (x16) carry 0 - so a x16
   pair reads 0x00vv00vv and a x8 pair reads 0xvvvv, matching how the bus
   interleaves the chips. */
void Intel28F128J3::EmitLaneByte(uint32_t addr, uint8_t v) {
    const uint32_t dw = DeviceWidth();
    for (uint32_t c = 0; c < Parallel(); ++c) {
        SaveAndWrite(addr + c * dw, v);
        for (uint32_t j = 1; j < dw; ++j) SaveAndWrite(addr + c * dw + j, 0);
    }
}

void Intel28F128J3::RestoreArray() {
    for (auto it = shadow_.rbegin(); it != shadow_.rend(); ++it) {
        if (uint8_t* p = Host(it->first)) *p = it->second;
    }
    shadow_.clear();
}

void Intel28F128J3::PresentStatus(uint32_t addr, uint8_t sr) {
    RestoreArray();
    EmitLaneByte(addr, sr);      /* SR.7 (WSMS) ready, plus any error bits */
}

uint32_t Intel28F128J3::BlockIndexChecked(uint32_t addr) {
    const uint32_t i = BlockIndex(addr);
    if (i >= block_locked_.size())
        HaltUnsupportedAccess("flash block index out of range", addr, 0);
    return i;
}

/* Read Identifier (0x90): manufacturer at the bank base, device at the next bus
   word, and each erase block's lock bit at block_base + kLockReadOffset. */
void Intel28F128J3::PresentId() {
    RestoreArray();
    const uint32_t bw = BusBytes();
    EmitLaneByte(MmioBase(),      uint8_t(kMfr    & 0xFFu));  /* identifier word 0 */
    EmitLaneByte(MmioBase() + bw, uint8_t(kDevice & 0xFFu));  /* identifier word 1 */
    const uint32_t n = NumBlocks();
    for (uint32_t i = 0; i < n; ++i)
        EmitLaneByte(BlockBase(i) + kLockReadOffset, uint8_t(block_locked_[i] & 0x01u));
}

void Intel28F128J3::PresentCfi() {
    static_assert(sizeof(kCfi) == kCfiCount, "kCfi must cover CFI words 0x10..0x3E");
    RestoreArray();
    const uint32_t bw  = BusBytes();
    const uint32_t win = MmioBase() + kCfiFirst * bw;
    for (uint32_t i = 0; i < kCfiCount; ++i) EmitLaneByte(win + i * bw, kCfi[i]);
}

/* NOR program clears bits only (1->0); commit into the backing, then present
   SR.7 at the same address. Read-array restores the committed value (saved by
   EmitLaneByte AFTER the commit) so the post-program read-back sees the data. */
void Intel28F128J3::CommitProgram(uint32_t addr, uint32_t value, uint32_t width) {
    RestoreArray();
    if (block_locked_[BlockIndexChecked(addr)]) {   /* datasheet p20: locked block aborts */
        PresentStatus(addr, kSrReady | kSrProgErr | kSrLockAbort);
        return;
    }
    uint8_t* h = Host(addr);
    if (h) for (uint32_t i = 0; i < width; ++i) h[i] &= uint8_t(value >> (8 * i));
    PresentStatus(addr);
}

void Intel28F128J3::EraseBlock(uint32_t addr) {
    const uint32_t blk   = EraseBlockBytes();
    const uint32_t bbase = addr & ~(blk - 1u);
    uint8_t* h = Host(bbase);
    if (h) std::memset(h, 0xFF, blk);
}

void Intel28F128J3::Command(uint32_t addr, uint32_t value, uint32_t width) {
    if (mode_ == Mode::kProgramSetup) {            /* this write carries the data */
        CommitProgram(addr, value, width);
        mode_ = Mode::kArray;
        return;
    }
    if (mode_ == Mode::kEraseSetup) {              /* expect confirm 0xD0 */
        if ((value & 0xFFu) == 0xD0u) {
            if (block_locked_[BlockIndexChecked(addr)]) {   /* datasheet p20: locked block aborts */
                PresentStatus(addr, kSrReady | kSrEraseErr | kSrLockAbort);
            } else {
                EraseBlock(addr);
                PresentStatus(addr);
            }
        }
        mode_ = Mode::kArray;
        return;
    }
    if (mode_ == Mode::kLockSetup) {               /* datasheet Table 4 */
        const uint32_t cmd = value & 0xFFu;
        if (cmd == 0x01u) {                        /* Set Block Lock-Bit */
            block_locked_[BlockIndexChecked(addr)] = 1u;
            PresentStatus(addr);
        } else if (cmd == 0xD0u) {                 /* Clear Block Lock-Bits: clears ALL (note 15) */
            block_locked_.assign(block_locked_.size(), 0u);
            PresentStatus(addr);
        } else {
            RestoreArray();                        /* 0xFF / abort -> array */
        }
        mode_ = Mode::kArray;
        return;
    }
    switch (value & 0xFFu) {                        /* chip decodes the low byte */
        case 0xFF: RestoreArray();              break;   /* read array */
        case 0x90: PresentId();                 break;   /* read id */
        case 0x98: PresentCfi();                break;   /* CFI query */
        case 0x70: PresentStatus(addr);         break;   /* read status */
        case 0x50:                              break;   /* clear status (ready) */
        case 0x40:
        case 0x10: mode_ = Mode::kProgramSetup; break;   /* word program setup */
        case 0x20: mode_ = Mode::kEraseSetup;   break;   /* block erase setup */
        case 0x60: mode_ = Mode::kLockSetup;
                   PresentStatus(addr);         break;   /* block lock/unlock setup */
        case 0xB0:
        case 0xD0:                              break;   /* suspend / stray confirm */
        default:
            HaltUnsupportedAccess("flash command", addr, value);
    }
}

uint8_t  Intel28F128J3::ReadByte(uint32_t addr) { return emu_.Get<EmulatedMemory>().ReadByte(addr); }
uint16_t Intel28F128J3::ReadHalf(uint32_t addr) { return emu_.Get<EmulatedMemory>().ReadHalf(addr); }
uint32_t Intel28F128J3::ReadWord(uint32_t addr) { return emu_.Get<EmulatedMemory>().ReadWord(addr); }
void Intel28F128J3::WriteByte(uint32_t addr, uint8_t  v) { Command(addr, v, 1); }
void Intel28F128J3::WriteHalf(uint32_t addr, uint16_t v) { Command(addr, v, 2); }
void Intel28F128J3::WriteWord(uint32_t addr, uint32_t v) { Command(addr, v, 4); }
