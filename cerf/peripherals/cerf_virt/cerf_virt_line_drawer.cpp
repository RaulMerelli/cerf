#include "cerf_virt_line_drawer.h"
#include "cerf_virt_blt_pixelops.h"
#include "cerf_virt_framebuffer.h"

#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"

namespace CerfVirt {

REGISTER_SERVICE(CerfVirtLineDrawer);

bool CerfVirtLineDrawer::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtLineDrawer::OnReady() {
    fb_  = &emu_.Get<CerfVirtFramebuffer>();
    mmu_ = &emu_.Get<ArmMmu>();
}

bool CerfVirtLineDrawer::Execute(const CerfLineDescriptor& l) {
    if (l.magic != kCerfLineMagic) {
        LOG(Periph, "[CerfVirtLineDrawer] bad line magic 0x%08X\n", l.magic);
        return false;
    }
    if (l.i_dir < 0 || l.i_dir > 7) {
        LOG(Periph, "[CerfVirtLineDrawer] bad iDir %d\n", l.i_dir);
        return false;
    }
    const uint32_t bits = BltPixelOps::FormatBits(l.dst.format);
    if (bits < 8u) {
        LOG(Periph, "[CerfVirtLineDrawer] unsupported dst format %u\n", l.dst.format);
        return false;
    }
    const uint32_t bpp = bits / 8u;
    Surface dst;
    if (!ResolveSurface(l.dst, bpp, &dst)) return false;

    const uint32_t base_mask = (bits >= 32u) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    const uint32_t pen       = l.solid_color & base_mask;
    const uint8_t  rop2_mark  = (uint8_t)l.mix;
    const uint8_t  rop2_space = (uint8_t)(l.mix >> 8);

    /* (maj_dx, maj_dy, min_dx, min_dy) per octant — the coordinate form of
       swblt's MajorDPtr/MinorDPtr/MajorDPixel/MinorDPixel (swline.cpp:87-100). */
    static const int8_t kDir[8][4] = {
        {  1,  0,  0,  1 }, {  0,  1,  1,  0 }, {  0,  1, -1,  0 }, { -1,  0,  0,  1 },
        { -1,  0,  0, -1 }, {  0, -1, -1,  0 }, {  0, -1,  1,  0 }, {  1,  0,  0, -1 },
    };
    const int8_t* dir = kDir[l.i_dir];

    long accum = (long)l.d_n + l.ll_gamma;
    const long axstp = (long)l.d_n;
    const long dgstp = (long)l.d_n - (long)l.d_m;

    int32_t x = l.x_start, y = l.y_start;
    int32_t style_state = l.style_state;
    for (int32_t n = l.c_pels; n > 0; --n) {
        const uint8_t rop2 = ((l.style >> ((uint32_t)(style_state++) & 31u)) & 1u)
                           ? rop2_space : rop2_mark;
        uint32_t run = 0;
        uint8_t* p = PixelPtr(dst, x, y, bpp, &run);
        if (p) {
            const uint32_t d = (run >= bpp) ? BltPixelOps::ReadPixel(p, bpp)
                                            : ReadStraddlePixel(dst, x, y, bpp);
            const uint32_t v = BltPixelOps::ApplyRop2(rop2, pen, d) & base_mask;
            if (run >= bpp) BltPixelOps::WritePixel(p, bpp, v);
            else            WriteStraddlePixel(dst, x, y, bpp, v);
        }
        if (n == 1) break;
        x += dir[0]; y += dir[1];
        if (axstp) {
            if (accum < 0) accum += axstp;
            else { x += dir[2]; y += dir[3]; accum += dgstp; }
        }
    }
    fb_->MarkDirty();
    return true;
}

}  /* namespace CerfVirt */
