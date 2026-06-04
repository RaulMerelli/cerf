#include "mediaq_mq1188_ge.h"

#include "mediaq_mq1188.h"
#include "../../core/log.h"

#include <cstring>

void MediaQMq1188Ge::WriteReg(uint32_t g, uint32_t value) {
    if (g >= kNumRegs) return;
    reg_[g] = value;
    if (g == kGe00Command) Execute();
}

uint32_t MediaQMq1188Ge::BytesPerPixel() const {
    /* GE0AR[31:30] colour depth: 00 = 8 bpp, 01 = 16 bpp (Reg 4-97). */
    switch ((reg_[kGe0ADstStride] >> 30) & 3u) {
        case 0:  return 1u;
        case 1:  return 2u;
        default: return 0u;
    }
}

/* 3-operand raster op (Reg 4-83 [7:0]): result bit = rop[(P<<2)|(S<<1)|D],
   evaluated bitwise across the pixel word. */
uint32_t MediaQMq1188Ge::Rop3(uint8_t rop, uint32_t p, uint32_t s, uint32_t d) {
    uint32_t r = 0u;
    if (rop & 0x01u) r |= ~p & ~s & ~d;
    if (rop & 0x02u) r |= ~p & ~s &  d;
    if (rop & 0x04u) r |= ~p &  s & ~d;
    if (rop & 0x08u) r |= ~p &  s &  d;
    if (rop & 0x10u) r |=  p & ~s & ~d;
    if (rop & 0x20u) r |=  p & ~s &  d;
    if (rop & 0x40u) r |=  p &  s & ~d;
    if (rop & 0x80u) r |=  p &  s &  d;
    return r;
}

void MediaQMq1188Ge::Execute() {
    /* A new command means the prior system-source command's source stream has
       completed; draw it now (using its register snapshot) before the live
       registers are overwritten. */
    if (pending_active_) ExecutePending();

    const uint32_t cmd  = reg_[kGe00Command];
    const uint32_t type = (cmd >> kCmdTypeShift) & kCmdTypeMask;
    const uint8_t  rop  = static_cast<uint8_t>(cmd & kCmdRopMask);

    const bool solid_pat = (cmd & kCmdSolidPat) != 0u;
    const bool src_sys   = (cmd & kCmdSrcSystem) != 0u;
    const bool mono_src  = (cmd & kCmdMonoSrc) != 0u;

    if (type == kTypeBitBlt && src_sys) {
        /* Source streams in after this command; latch a snapshot and draw it
           from PushSourceFifo once the expected source-dword count arrives. */
        std::memcpy(pending_reg_, reg_, sizeof(reg_));
        src_fifo_.clear();
        expected_dwords_ = ExpectedSourceDwords();
        pending_active_ = true;
        if (expected_dwords_ == 0u) ExecutePending();   /* empty blit. */
        return;
    }

    if (type == kTypeBitBlt && solid_pat) {
        FillSolid(rop, reg_[kGe12PatFg], cmd);
        return;
    }

    if (type == kTypeLine) {
        DrawLine(cmd);
        return;
    }

    if (type == kTypeBitBlt) {
        /* src_sys and solid_pat are handled above; a BitBLT reaching here takes
           its source from display memory — a monochrome bitmap expanded to
           fg/bg, or a screen-to-screen colour copy. */
        if (mono_src) BlitMonoFromDisplay(cmd);
        else          BlitColorFromDisplay(cmd);
        return;
    }

    LOG(Caution, "MediaQ GE: unimplemented command 0x%08X (type=%u rop=0x%02X "
                 "solidPat=%u srcSys=%u monoSrc=%u)\n",
        cmd, type, rop, solid_pat ? 1u : 0u, src_sys ? 1u : 0u, mono_src ? 1u : 0u);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void MediaQMq1188Ge::PushSourceFifo(uint32_t value) {
    src_fifo_.push_back(value);
    if (pending_active_ && src_fifo_.size() >= expected_dwords_) ExecutePending();
}

void MediaQMq1188Ge::FlushPending() {
    if (pending_active_ && src_fifo_.size() >= expected_dwords_) ExecutePending();
}

/* Source dwords the latched system-source blit will stream: contiguous 1-bpp
   bits for a mono source (sub_184550C), or 16-bpp pixels two-per-dword for a
   colour source (sub_1845A9C), with the first pixel at GE09R bit 4. */
uint32_t MediaQMq1188Ge::ExpectedSourceDwords() const {
    const uint32_t w = reg_[kGe01Size] & 0xFFFu;
    const uint32_t h = (reg_[kGe01Size] >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return 0u;
    if (reg_[kGe00Command] & kCmdMonoSrc) {
        const uint32_t g09 = reg_[kGe09SrcStride];
        const uint32_t bit_off0 = g09 & 0x1Fu;
        const uint32_t stride_bits = w + (g09 >> 25);
        const uint32_t total_bits = bit_off0 + (h - 1u) * stride_bits + w;
        return (total_bits + 31u) / 32u;
    }
    const uint32_t start_px = (reg_[kGe09SrcStride] >> 4) & 1u;
    const uint32_t dwords_per_row = (start_px + ((start_px + w) & 1u) + w) / 2u;
    return dwords_per_row * h;
}

void MediaQMq1188Ge::ExecutePending() {
    pending_active_ = false;
    if (pending_reg_[kGe00Command] & kCmdMonoSrc)
        BlitMonoSource(pending_reg_);
    else
        BlitColorSource(pending_reg_);
    src_fifo_.clear();
}
