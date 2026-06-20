#define NOMINMAX

#include "siemens_mp377_touch_panel.h"
#include "siemens_mp377_sm501.h"

#include "../../peripherals/peripheral_base.h"
#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"

#include <atomic>
#include <cstdint>

#pragma warning(disable: 4100 4189 4505)

namespace siemens_mp377 {

static std::atomic<uint32_t> g_mp377_smi_last_cmd{0xE0u};
static std::atomic<uint32_t> g_mp377_smi_read_phase{0};

static std::atomic<uint32_t> g_mp377_touch_down{0};
static std::atomic<uint32_t> g_mp377_touch_penup_reads{0};
static std::atomic<uint32_t> g_mp377_touch_x{0};
static std::atomic<uint32_t> g_mp377_touch_y{0};

static uint16_t g_mp377_smi_cmd_q[16] = {};
static uint32_t g_mp377_smi_cmd_head = 0;
static uint32_t g_mp377_smi_cmd_tail = 0;

static bool Mp377EffectiveTouchDown() {
    return g_mp377_touch_down.load(std::memory_order_acquire) != 0u;
}

void Mp377QueueSmiCommand(uint16_t cmd) {
    const uint32_t next = (g_mp377_smi_cmd_tail + 1u) & 15u;
    if (next == g_mp377_smi_cmd_head)
        g_mp377_smi_cmd_head = (g_mp377_smi_cmd_head + 1u) & 15u;
    g_mp377_smi_cmd_q[g_mp377_smi_cmd_tail] = cmd;
    g_mp377_smi_cmd_tail = next;
    g_mp377_smi_last_cmd.store(cmd, std::memory_order_relaxed);

    /* sub_29E27C0 starts each ADC sample burst by writing the three commands
       stored at touch.dll unk_29E50CC.  Observed runtime order for that table
       is D3, D0, 93; D3 is the start of the burst, so align the following
       SMI data reads from phase zero there. */
    if ((cmd & 0xFFu) == 0xD3u)
        g_mp377_smi_read_phase.store(0u, std::memory_order_release);
}

static uint16_t Mp377PopSmiCommand() {
    if (g_mp377_smi_cmd_head == g_mp377_smi_cmd_tail)
        return static_cast<uint16_t>(g_mp377_smi_last_cmd.load(std::memory_order_relaxed));
    const uint16_t cmd = g_mp377_smi_cmd_q[g_mp377_smi_cmd_head];
    g_mp377_smi_cmd_head = (g_mp377_smi_cmd_head + 1u) & 15u;
    g_mp377_smi_last_cmd.store(cmd, std::memory_order_relaxed);
    return cmd;
}

static uint16_t Mp377HostCoordToTouchRaw4(uint32_t v, uint32_t vmax) {
    if (v > vmax) v = vmax;

    /* touch.dll uses the sampled values as calibration-space coordinates and
       later divides final screen coordinates by four, so expose host
       coordinates in the same x4 coordinate space. */
    uint32_t raw = v * 4u;
    const uint32_t max_raw = vmax * 4u;
    if (raw > max_raw) raw = max_raw;
    return static_cast<uint16_t>(raw);
}

uint32_t Mp377TouchReadPenDetectReg() {
    /* touch.dll maps 0xFFD82480 and reads +4.  Bit 3 set means pen-up.

       A permanent pen-up value at idle makes the touch IST wake/spin and can
       starve GUI rendering. Emit pen-up only for a short window after a real
       host release so touch.dll can produce the UP sample, then return to
       idle=0. */
    if (Mp377EffectiveTouchDown()) {
        return 0x00000000u;
    }

    uint32_t n = g_mp377_touch_penup_reads.load(std::memory_order_acquire);
    while (n != 0u) {
        if (g_mp377_touch_penup_reads.compare_exchange_weak(
                n, n - 1u, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                return 0x00000008u;
            }
    }

    return 0x00000000u;
}

static uint16_t Mp377SmiNextAdcTransportHalfword() {
    if (!Mp377EffectiveTouchDown()) {
        return 0u;
    }

    const uint32_t x = g_mp377_touch_x.load(std::memory_order_relaxed);
    const uint32_t y = g_mp377_touch_y.load(std::memory_order_relaxed);

    const uint16_t raw_x = Mp377HostCoordToTouchRaw4(x, kFbWidth  - 1u);
    const uint16_t raw_y = Mp377HostCoordToTouchRaw4(y, kFbHeight - 1u);

    /* Source-grounded touch.dll layout:
         sub_29E27C0 reads three 32-bit SMI values into v14[0..5].
         It accepts the burst only when:
             v14[1] != 0 && v14[2] != 0 && v14[4] != 0 && v14[5] != 0
         It exports:
             X samples = v14[1], v14[2]
             Y samples = v14[4], v14[5]

       Source-grounded smibase.dll transport:
         op 2 read stores LOW16 values from consecutive data-register reads.

       Therefore the six low16 transport slots for one burst are:
             v14[0] filler
             v14[1] X
             v14[2] X
             v14[3] filler
             v14[4] Y
             v14[5] Y
    */
    const uint32_t phase = g_mp377_smi_read_phase.fetch_add(1u, std::memory_order_acq_rel) % 6u;
    uint16_t out = 0u;
    switch (phase) {
    case 1:
    case 2:
        out = raw_x;
        break;
    case 4:
    case 5:
        out = raw_y;
        break;
    default:
        out = 0u;
        break;
    }
    return out;
}

uint32_t Mp377TouchReadSmiSampleWord() {
    (void)Mp377PopSmiCommand();

    const uint16_t sample = Mp377SmiNextAdcTransportHalfword();


    /* smibase consumes only the low 16 bits of each hardware data-register read. */
    return static_cast<uint32_t>(sample);
}

void Mp377TouchUpdateHostPointer(int x, int y, bool down) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= static_cast<int>(kFbWidth))  x = static_cast<int>(kFbWidth) - 1;
    if (y >= static_cast<int>(kFbHeight)) y = static_cast<int>(kFbHeight) - 1;

    g_mp377_touch_x.store(static_cast<uint32_t>(x), std::memory_order_relaxed);
    g_mp377_touch_y.store(static_cast<uint32_t>(y), std::memory_order_relaxed);

    const bool was_down = g_mp377_touch_down.exchange(down ? 1u : 0u, std::memory_order_acq_rel) != 0u;
    if (down != was_down) {
        g_mp377_smi_read_phase.store(0u, std::memory_order_release);

        if (!down && was_down)
            g_mp377_touch_penup_reads.store(16u, std::memory_order_release);
        else if (down)
            g_mp377_touch_penup_reads.store(0u, std::memory_order_release);
    }
}

void Mp377TouchCaptureLost() {
    const bool was_down = g_mp377_touch_down.exchange(0u, std::memory_order_acq_rel) != 0u;
    if (was_down) {
        g_mp377_smi_read_phase.store(0u, std::memory_order_release);
        g_mp377_touch_penup_reads.store(16u, std::memory_order_release);
    }
}

} // namespace siemens_mp377

