#include <windows.h>
#include "cerf_regs_map.h"
#include <string.h>

/* Register offsets/struct below MUST match cerf/peripherals/cerf_virt/cerf_virt_cursor_regs.h. */
#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"
#define CERF_CUR_DESC_VA      0x000u
#define CERF_CUR_KICK         0x004u

#define CERF_CUR_MAX_DIM      64u
#define CERF_CUR_MAX_STRIDE   ((CERF_CUR_MAX_DIM + 7u) / 8u)
#define CERF_CUR_BITS_BYTES   (CERF_CUR_MAX_STRIDE * CERF_CUR_MAX_DIM * 2u)

typedef struct {
    DWORD visible;
    DWORD cx;
    DWORD cy;
    DWORD xhot;
    DWORD yhot;
    DWORD stride;
    BYTE  bits[CERF_CUR_BITS_BYTES];
} CerfCursorDesc;

static volatile ULONG* s_cur_regs = NULL;
static CerfCursorDesc   s_cur_desc;

static BOOL CerfMapCursorRegs(void) {
    if (s_cur_regs) return TRUE;
    s_cur_regs = (volatile ULONG*)CerfMapRegsPage(g_CerfVirtBase + CerfVirt::kCursorOffset,
                                                  CerfVirt::kCursorSize);
    return s_cur_regs != NULL;
}

/* The GPE mask stride is often NEGATIVE (bottom-up surface); normalize into a
   top-down positive-stride buffer. src+row*stride walks correctly for either
   sign (logical rows [0,cy) AND, [cy,2cy) XOR), so copying dst_stride bytes per
   row drops the sign without misreading rows. visible=FALSE hides the cursor. */
extern "C" void CerfPublishCursor(const void* mask_bits, int stride,
                                  int cx, int cy, int xhot, int yhot, BOOL visible) {
    if (!CerfMapCursorRegs()) return;

    DWORD dst_stride = (cx > 0) ? (DWORD)((cx + 7) / 8) : 0u;

    s_cur_desc.cx     = (DWORD)cx;
    s_cur_desc.cy     = (DWORD)cy;
    s_cur_desc.xhot   = (DWORD)xhot;
    s_cur_desc.yhot   = (DWORD)yhot;
    s_cur_desc.stride = dst_stride;
    s_cur_desc.visible = (visible && mask_bits && cx > 0 && cy > 0 &&
                          dst_stride <= CERF_CUR_MAX_STRIDE &&
                          (DWORD)cy <= CERF_CUR_MAX_DIM) ? 1u : 0u;

    if (s_cur_desc.visible) {
        const BYTE* src = (const BYTE*)mask_bits;
        for (int row = 0; row < cy; ++row) {
            memcpy(s_cur_desc.bits + (DWORD)row * dst_stride,
                   src + row * stride, dst_stride);                 /* AND row */
            memcpy(s_cur_desc.bits + ((DWORD)cy + (DWORD)row) * dst_stride,
                   src + (cy + row) * stride, dst_stride);          /* XOR row */
        }
    }
    CERF_LOG_X_DEV("cerf_guest: cursor publish cy", (DWORD)cy);

    s_cur_regs[CERF_CUR_DESC_VA / 4] = (ULONG)(ULONG_PTR)&s_cur_desc;
    s_cur_regs[CERF_CUR_KICK / 4]    = 1u;
}
