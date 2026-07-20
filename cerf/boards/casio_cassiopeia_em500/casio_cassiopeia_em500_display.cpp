#include "casio_cassiopeia_em500_display.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../host/host_window.h"
#include "../../state/state_stream.h"

#include <cstring>

namespace {

constexpr uint32_t kOffPanelEnable = 0x0980u;
constexpr uint32_t kOffPanel0984   = 0x0984u;
constexpr uint32_t kOffPanel0988   = 0x0988u;
constexpr uint32_t kOffBrightness  = 0x098Cu;
constexpr uint32_t kOffPanel0994   = 0x0994u;
constexpr uint32_t kOffPanel0998   = 0x0998u;   /* nk_main_kernel.exe sub_9F034498 @0x9F0344EC (sw 0) */
constexpr uint32_t kOffContrast    = 0x099Cu;

constexpr uint32_t kOffBlitGo  = 0x0A00u;
constexpr uint32_t kOffBlitOp  = 0x0A04u;
constexpr uint32_t kOffBlitLen = 0x0A08u;
constexpr uint32_t kOffBlitSrc = 0x0A10u;
constexpr uint32_t kOffBlitDst = 0x0A14u;

/* ddi.dll sub_FC4E38 @0xFC4F30 (v10[1] = 129). */
constexpr uint32_t kBlitOpCopy = 0x81u;

constexpr uint32_t kPaMask = 0x1FFFFFFFu;   /* VR4131 UM Fig 3-1 kseg fold */

}  /* namespace */

void CasioCassiopeiaEm500Display::Init(CerfEmulator& emu) {
    emu_ = &emu;
    fb_.assign(kFbSize, 0u);
}

bool CasioCassiopeiaEm500Display::TryReadByte(uint32_t off, uint8_t& out) {
    if (!InFb(off)) return false;
    out = fb_[off - kFbOffset];
    return true;
}

bool CasioCassiopeiaEm500Display::TryReadHalf(uint32_t off, uint16_t& out) {
    if (!InFb(off)) return false;
    std::memcpy(&out, &fb_[off - kFbOffset], sizeof(out));
    return true;
}

bool CasioCassiopeiaEm500Display::TryReadWord(uint32_t off, uint32_t& out) {
    if (InFb(off)) {
        std::memcpy(&out, &fb_[off - kFbOffset], sizeof(out));
        return true;
    }
    /* ddi.dll sub_FC4E38 @0xFC4F44: busy poll until 0; the copy completes at GO. */
    if (off == kOffBlitGo) { out = 0u; return true; }
    return false;
}

bool CasioCassiopeiaEm500Display::TryWriteByte(uint32_t off, uint8_t value) {
    if (!InFb(off)) return false;
    fb_[off - kFbOffset] = value;
    return true;
}

bool CasioCassiopeiaEm500Display::TryWriteHalf(uint32_t off, uint16_t value) {
    if (!InFb(off)) return false;
    std::memcpy(&fb_[off - kFbOffset], &value, sizeof(value));
    return true;
}

bool CasioCassiopeiaEm500Display::TryWriteWord(uint32_t off, uint32_t value) {
    if (InFb(off)) {
        std::memcpy(&fb_[off - kFbOffset], &value, sizeof(value));
        return true;
    }
    switch (off) {
        case kOffPanelEnable: reg_0980_ = value; MaybePublishDisplaySize(); return true;
        case kOffPanel0984:   reg_0984_ = value; return true;
        case kOffPanel0988:   reg_0988_ = value; return true;
        case kOffBrightness:  reg_098C_ = value; return true;
        case kOffPanel0994:   reg_0994_ = value; return true;
        case kOffContrast:    reg_099C_ = value; return true;
        case kOffPanel0998:   return true;
        case kOffBlitOp:      blit_op_        = value; return true;
        case kOffBlitLen:     blit_len_words_ = value; return true;
        case kOffBlitSrc:     blit_src_       = value; return true;
        case kOffBlitDst:     blit_dst_       = value; return true;
        case kOffBlitGo:
            /* ddi.dll sub_FC4E38 @0xFC4F38: only 1 is written to GO. */
            if (value != 1u) {
                LOG(Caution, "EM-500 display blit GO value 0x%X\n", value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            RunBlit();
            return true;
        default: return false;
    }
}

void CasioCassiopeiaEm500Display::RunBlit() {
    if (blit_op_ != kBlitOpCopy) {
        LOG(Caution, "EM-500 display blit opcode 0x%X unsupported\n", blit_op_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint32_t bytes = blit_len_words_ * 4u;
    if (bytes == 0u) return;
    const uint32_t src_pa = blit_src_ & kPaMask;
    if (static_cast<uint64_t>(blit_dst_) + bytes > kFbSize) {
        LOG(Caution, "EM-500 display blit dst=0x%X len=%u exceeds framebuffer\n",
            blit_dst_, bytes);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint8_t* host_src = emu_->Get<EmulatedMemory>().TryTranslate(src_pa);
    if (!host_src) {
        LOG(Caution, "EM-500 display blit src_pa=0x%X unbacked\n", src_pa);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    std::memcpy(fb_.data() + blit_dst_, host_src, bytes);
}

void CasioCassiopeiaEm500Display::MaybePublishDisplaySize() {
    if (size_published_ || !IsDisplayEnabled()) return;
    size_published_ = true;
    emu_->Get<HostWindow>().OnLcdEnabled(GuestW(), GuestH());
}

void CasioCassiopeiaEm500Display::SaveState(StateWriter& w) const {
    w.Write<uint64_t>(fb_.size());
    if (!fb_.empty()) w.WriteBytes(fb_.data(), fb_.size());
    w.Write(blit_op_); w.Write(blit_len_words_);
    w.Write(blit_src_); w.Write(blit_dst_);
    w.Write(reg_0980_); w.Write(reg_0984_); w.Write(reg_0988_);
    w.Write(reg_098C_); w.Write(reg_0994_); w.Write(reg_099C_);
    w.Write<uint8_t>(size_published_ ? 1u : 0u);
}

void CasioCassiopeiaEm500Display::RestoreState(StateReader& r) {
    uint64_t n = 0;
    r.Read(n);
    fb_.assign(static_cast<size_t>(n), 0u);
    if (n) r.ReadBytes(fb_.data(), static_cast<size_t>(n));
    r.Read(blit_op_); r.Read(blit_len_words_);
    r.Read(blit_src_); r.Read(blit_dst_);
    r.Read(reg_0980_); r.Read(reg_0984_); r.Read(reg_0988_);
    r.Read(reg_098C_); r.Read(reg_0994_); r.Read(reg_099C_);
    uint8_t pub = 0;
    r.Read(pub);
    size_published_ = pub != 0;
}
