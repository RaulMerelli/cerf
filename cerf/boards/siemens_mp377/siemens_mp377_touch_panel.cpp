#define NOMINMAX

#include "siemens_mp377_touch_panel.h"
#include "siemens_mp377_sm501.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"
#include "../../socs/irq_controller.h"

#include <cstdint>

namespace siemens_mp377 {

namespace {

struct TouchPoint {
    int32_t x;
    int32_t y;
};

struct TouchCalibrationProfile {
    SiemensMp377PanelProfile profile;
    TouchPoint raw_points[5];
};

struct TouchAffineInverse {
    double xx;
    double xy;
    double x0;
    double yx;
    double yy;
    double y0;
};

static constexpr TouchCalibrationProfile kTouchCalibrationProfiles[] = {
    {SiemensMp377PanelProfile::Inch12_800x600, {{1855, 1859}, {3023, 3008}, {3034, 676}, {672, 672}, {677, 3011}}},
    {SiemensMp377PanelProfile::Inch15_1024x768, {{1879, 1844}, {3173, 3073}, {3175, 655}, {585, 654}, {586, 3035}}},
    {SiemensMp377PanelProfile::Inch19_1280x1024, {{1842, 1838}, {2937, 2976}, {2936, 717}, {697, 713}, {712, 2994}}},
};

const TouchCalibrationProfile& CurrentTouchCalibrationProfile() {
    for (const auto& profile : kTouchCalibrationProfiles) {
        if (profile.profile == kMp377HwiPanelProfile) return profile;
    }
    return kTouchCalibrationProfiles[0];
}

TouchPoint DisplayCalibrationPoint(const TouchCalibrationProfile& profile, uint32_t index) {
    const uint32_t panel_width = Mp377PanelWidth(profile.profile);
    const uint32_t panel_height = Mp377PanelHeight(profile.profile);
    const int32_t x_inset = static_cast<int32_t>(2u * (panel_width / 20u));
    const int32_t y_inset = static_cast<int32_t>(2u * (panel_height / 20u));
    const int32_t width = static_cast<int32_t>(panel_width);
    const int32_t height = static_cast<int32_t>(panel_height);

    switch (index) {
    case 1: return {x_inset, y_inset};
    case 2: return {x_inset, height - y_inset};
    case 3: return {width - x_inset, height - y_inset};
    case 4: return {width - x_inset, y_inset};
    default: return {width / 2, height / 2};
    }
}

bool Solve3x3(const double in_a[3][3], const double in_b[3], double out[3]) {
    double a[3][4] = {};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col)
            a[row][col] = in_a[row][col];
        a[row][3] = in_b[row];
    }

    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        double pivot_abs = a[pivot][col] < 0.0 ? -a[pivot][col] : a[pivot][col];
        for (int row = col + 1; row < 3; ++row) {
            const double v = a[row][col] < 0.0 ? -a[row][col] : a[row][col];
            if (v > pivot_abs) {
                pivot_abs = v;
                pivot = row;
            }
        }
        if (pivot_abs < 1e-12) return false;
        if (pivot != col) {
            for (int i = col; i < 4; ++i) {
                const double tmp = a[col][i];
                a[col][i] = a[pivot][i];
                a[pivot][i] = tmp;
            }
        }

        const double div = a[col][col];
        for (int i = col; i < 4; ++i)
            a[col][i] /= div;

        for (int row = 0; row < 3; ++row) {
            if (row == col) continue;
            const double factor = a[row][col];
            for (int i = col; i < 4; ++i)
                a[row][i] -= factor * a[col][i];
        }
    }

    for (int i = 0; i < 3; ++i)
        out[i] = a[i][3];
    return true;
}

TouchAffineInverse BuildTouchAffineInverse(const TouchCalibrationProfile& profile) {
    double ata[3][3] = {};
    double atx[3] = {};
    double aty[3] = {};

    for (uint32_t i = 0; i < 5u; ++i) {
        const TouchPoint& raw = profile.raw_points[i];
        const TouchPoint display = DisplayCalibrationPoint(profile, i);
        const double r[3] = {static_cast<double>(raw.x), static_cast<double>(raw.y), 1.0};

        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col)
                ata[row][col] += r[row] * r[col];
            atx[row] += r[row] * static_cast<double>(display.x);
            aty[row] += r[row] * static_cast<double>(display.y);
        }
    }

    double x_coeff[3] = {};
    double y_coeff[3] = {};
    if (!Solve3x3(ata, atx, x_coeff) || !Solve3x3(ata, aty, y_coeff)) return {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    const double a = x_coeff[0];
    const double b = x_coeff[1];
    const double c = x_coeff[2];
    const double d = y_coeff[0];
    const double e = y_coeff[1];
    const double f = y_coeff[2];
    const double det = a * e - b * d;
    if (det > -1e-12 && det < 1e-12) return {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    return {
        e / det, -b / det, (b * f - e * c) / det, -d / det, a / det, (d * c - a * f) / det,
    };
}

uint16_t ClampAdc12(double value) {
    int32_t rounded = static_cast<int32_t>(value >= 0.0 ? value + 0.5 : value - 0.5);
    if (rounded < 1) rounded = 1;
    if (rounded > 4095) rounded = 4095;
    return static_cast<uint16_t>(rounded);
}

} // namespace

bool SiemensMp377TouchPanel::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SiemensMP377;
}

void SiemensMp377TouchPanel::OnReady() {
    const TouchAffineInverse affine = BuildTouchAffineInverse(CurrentTouchCalibrationProfile());
    touch_affine_[0] = affine.xx;
    touch_affine_[1] = affine.xy;
    touch_affine_[2] = affine.x0;
    touch_affine_[3] = affine.yx;
    touch_affine_[4] = affine.yy;
    touch_affine_[5] = affine.y0;
}

bool SiemensMp377TouchPanel::EffectiveTouchDown() const {
    return touch_down_.load(std::memory_order_acquire) != 0u;
}

void SiemensMp377TouchPanel::QueueSmiCommand(uint16_t cmd) {
    const uint32_t next = (smi_cmd_tail_ + 1u) & 15u;
    if (next == smi_cmd_head_) smi_cmd_head_ = (smi_cmd_head_ + 1u) & 15u;
    smi_cmd_q_[smi_cmd_tail_] = cmd;
    smi_cmd_tail_ = next;
    smi_last_cmd_.store(cmd, std::memory_order_relaxed);

    /* sub_29E27C0 starts each ADC sample burst by writing the three commands
       stored at siemens_mp377_v1040 touch.dll unk_29E50CC.  Observed runtime order for that table
       is D3, D0, 93; D3 is the start of the burst, so align the following
       SMI data reads from phase zero there. */
    if ((cmd & 0xFFu) == 0xD3u) smi_read_phase_.store(0u, std::memory_order_release);
}

uint16_t SiemensMp377TouchPanel::PopSmiCommand() {
    if (smi_cmd_head_ == smi_cmd_tail_) return static_cast<uint16_t>(smi_last_cmd_.load(std::memory_order_relaxed));
    const uint16_t cmd = smi_cmd_q_[smi_cmd_head_];
    smi_cmd_head_ = (smi_cmd_head_ + 1u) & 15u;
    smi_last_cmd_.store(cmd, std::memory_order_relaxed);
    return cmd;
}

void SiemensMp377TouchPanel::HostPointToTouchRaw(uint32_t x, uint32_t y, uint16_t* raw_x, uint16_t* raw_y) const {
    /* touch.dll owns the GWES calibration step.  The emulated hardware must
       return raw 12-bit ADC coordinates.  The calibration profile below comes
       from default.hv CalibrationData{12,15,19}in, and the display target
       points come from siemens_mp377_v1040 touch.dll sub_29E2474: center, then 10%/90% corners. */
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    *raw_x = ClampAdc12(touch_affine_[0] * dx + touch_affine_[1] * dy + touch_affine_[2]);
    *raw_y = ClampAdc12(touch_affine_[3] * dx + touch_affine_[4] * dy + touch_affine_[5]);
}

uint32_t SiemensMp377TouchPanel::ReadPenDetectReg() {
    /* siemens_mp377_v1040 touch.dll maps 0xFFD82480 and reads +4. Bit 3 set means pen-up.
       Reading the latched state acknowledges the raw touch IRQ; new host
       down/move/up events assert a fresh edge from SiemensMp377TouchInput. */
    emu_.Get<IrqController>().DeAssertIrq(kTouchIrqSource);
    return EffectiveTouchDown() ? 0x00000000u : 0x00000008u;
}

uint16_t SiemensMp377TouchPanel::NextAdcTransportHalfword() {
    if (!EffectiveTouchDown()) return 0u;

    const uint32_t x = touch_x_.load(std::memory_order_relaxed);
    const uint32_t y = touch_y_.load(std::memory_order_relaxed);

    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    HostPointToTouchRaw(x, y, &raw_x, &raw_y);

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
    const uint32_t phase = smi_read_phase_.fetch_add(1u, std::memory_order_acq_rel) % 6u;
    switch (phase) {
    case 1:
    case 2: return raw_x;
    case 4:
    case 5: return raw_y;
    default: return 0u;
    }
}

uint32_t SiemensMp377TouchPanel::ReadSmiSampleWord() {
    (void)PopSmiCommand();

    /* smibase consumes only the low 16 bits of each hardware data-register read. */
    return static_cast<uint32_t>(NextAdcTransportHalfword());
}

void SiemensMp377TouchPanel::UpdateHostPointer(int x, int y, bool down) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= static_cast<int>(kFbWidth)) x = static_cast<int>(kFbWidth) - 1;
    if (y >= static_cast<int>(kFbHeight)) y = static_cast<int>(kFbHeight) - 1;

    touch_x_.store(static_cast<uint32_t>(x), std::memory_order_relaxed);
    touch_y_.store(static_cast<uint32_t>(y), std::memory_order_relaxed);

    const bool was_down = touch_down_.exchange(down ? 1u : 0u, std::memory_order_acq_rel) != 0u;
    if (down != was_down) smi_read_phase_.store(0u, std::memory_order_release);
}

void SiemensMp377TouchPanel::CaptureLost() {
    const bool was_down = touch_down_.exchange(0u, std::memory_order_acq_rel) != 0u;
    if (was_down) {
        smi_read_phase_.store(0u, std::memory_order_release);
    }
}

void SiemensMp377TouchPanel::SaveState(StateWriter& w) const {
    uint32_t v = smi_last_cmd_.load(std::memory_order_acquire);
    w.Write(v);
    v = smi_read_phase_.load(std::memory_order_acquire);
    w.Write(v);
    v = touch_down_.load(std::memory_order_acquire);
    w.Write(v);
    v = touch_x_.load(std::memory_order_acquire);
    w.Write(v);
    v = touch_y_.load(std::memory_order_acquire);
    w.Write(v);
    w.WriteBytes(smi_cmd_q_, sizeof(smi_cmd_q_));
    w.Write(smi_cmd_head_);
    w.Write(smi_cmd_tail_);
}

void SiemensMp377TouchPanel::RestoreState(StateReader& r) {
    uint32_t v = 0;
    r.Read(v);
    smi_last_cmd_.store(v, std::memory_order_release);
    r.Read(v);
    smi_read_phase_.store(v, std::memory_order_release);
    r.Read(v);
    touch_down_.store(v ? 1u : 0u, std::memory_order_release);
    r.Read(v);
    touch_x_.store(v, std::memory_order_release);
    r.Read(v);
    touch_y_.store(v, std::memory_order_release);
    r.ReadBytes(smi_cmd_q_, sizeof(smi_cmd_q_));
    r.Read(smi_cmd_head_);
    r.Read(smi_cmd_tail_);
    smi_cmd_head_ &= 15u;
    smi_cmd_tail_ &= 15u;
}

REGISTER_SERVICE(SiemensMp377TouchPanel);

} // namespace siemens_mp377
