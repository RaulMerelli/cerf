#define NOMINMAX

#include "siemens_mp377_sm501.h"
#include "siemens_mp377_touch_panel.h"

#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/irq_controller.h"

#include <cstdint>
#include <atomic>
#include <algorithm>
#include <vector>

#pragma warning(disable: 4100 4189 4505)

namespace {

constexpr uint32_t kFbPa      = 0xCA000000u;
constexpr uint32_t kFbBytes   = 0x01000000u;
constexpr uint32_t kRegPa     = 0xC8000000u;
constexpr uint32_t kRegBytes  = 0x00200000u;

constexpr uint32_t kFbWidth   = 640u;
constexpr uint32_t kFbHeight  = 480u;
constexpr uint32_t kFbStride  = kFbWidth * 2u;

constexpr int kMp377SmiCascadeSource = 24;
constexpr uint32_t kMp377Sm501MasterSmiBit = 0x00000100u;
constexpr uint32_t kMp377SmiBridgeStatusBit = 0x00000001u;

static std::atomic<uint32_t> g_mp377_smi_master_pending{0u};
static std::atomic<uint32_t> g_mp377_smi_bridge_status{0u};
static std::atomic<uint32_t> g_mp377_smi_bridge_enable{0xFFFFFFFFu};
static std::atomic<CerfEmulator*> g_mp377_smi_emu{nullptr};

static void Mp377SmiDeassertCascade() {
    CerfEmulator* emu = g_mp377_smi_emu.load(std::memory_order_acquire);
    if (emu) emu->Get<IrqController>().DeAssertIrq(kMp377SmiCascadeSource);
}

static void Mp377SmiClearPending() {
    g_mp377_smi_master_pending.store(0u, std::memory_order_release);
    g_mp377_smi_bridge_status.store(0u, std::memory_order_release);
    Mp377SmiDeassertCascade();
}

static void Mp377SmiAssertCascade() {
    CerfEmulator* emu = g_mp377_smi_emu.load(std::memory_order_acquire);
    if (emu) emu->Get<IrqController>().AssertIrq(kMp377SmiCascadeSource);
}

class SiemensMp377Sm501Fb : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }
    void OnReady() override {
        vram_.assign(kFbBytes, 0u);
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kFbPa; }
    uint32_t MmioSize() const override { return kFbBytes; }

    uint8_t  ReadByte(uint32_t a) override { return vram_[CpuVramOffset(a)]; }
    uint16_t ReadHalf(uint32_t a) override {
        const size_t i = CpuVramOffset(a);
        return static_cast<uint16_t>(vram_[i] | (vram_[i + 1] << 8));
    }
    uint32_t ReadWord(uint32_t a) override {
        const size_t i = CpuVramOffset(a);
        return static_cast<uint32_t>(vram_[i] | (vram_[i + 1] << 8) |
                                     (vram_[i + 2] << 16) | (vram_[i + 3] << 24));
    }
    void WriteByte(uint32_t a, uint8_t v) override {
        const size_t i = CpuVramOffset(a);
        vram_[i] = v;
        NoteWrite(static_cast<uint32_t>(i));
    }
    void WriteHalf(uint32_t a, uint16_t v) override {
        const size_t i = CpuVramOffset(a);
        vram_[i] = static_cast<uint8_t>(v);
        vram_[i + 1] = static_cast<uint8_t>(v >> 8);
        NoteWrite(static_cast<uint32_t>(i));
    }
    void WriteWord(uint32_t a, uint32_t v) override {
        const size_t i = CpuVramOffset(a);
        vram_[i]     = static_cast<uint8_t>(v);
        vram_[i + 1] = static_cast<uint8_t>(v >> 8);
        vram_[i + 2] = static_cast<uint8_t>(v >> 16);
        vram_[i + 3] = static_cast<uint8_t>(v >> 24);
        NoteWrite(static_cast<uint32_t>(i));
    }

    const uint8_t* Vram() const { return vram_.data(); }
    uint8_t* MutableVramFor2d() { return vram_.data(); }
    bool WasWritten() const { return written_; }

    void Note2dWrite(uint32_t off, uint32_t bytes) {
        if (bytes == 0) return;
        NoteWrite(off);
        NoteWrite(off + bytes - 1u);
    }

private:
    static constexpr uint32_t kDdiCpuVramApertureBiasBytes = 8u;
    static constexpr uint32_t kDdiCpuVramAperturePa = kFbPa + kDdiCpuVramApertureBiasBytes;

    static bool IsDdiCpuApertureAddress(uint32_t a) {
        return a >= kDdiCpuVramAperturePa && a < kFbPa + kFbBytes;
    }

    static uint32_t DdiCpuPointerToSm501Offset(uint32_t a) {
        // ddi_vgx models software-visible VRAM as:
        //   cpu_pointer = MEMORY[0x1E64D50] + sm501_offset
        // and converts it back for SM501 with:
        //   sm501_offset = cpu_pointer - MEMORY[0x1E64D50]
        // In this emulator that CPU aperture is observed at kFbPa + 8.
        return a - kDdiCpuVramAperturePa;
    }

    static uint32_t CpuVramOffset(uint32_t a) {
        if (IsDdiCpuApertureAddress(a))
            return DdiCpuPointerToSm501Offset(a);
        return a - kFbPa;
    }

    void NoteWrite(uint32_t) { written_ = true; }

    std::vector<uint8_t> vram_;
    bool written_ = false;
};

class SiemensMp377Sm501Regs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }
    void OnReady() override {
        siemens_mp377::Mp377SmiBindEmulator(&emu_);
        regs_.assign(kRegBytes / 4u, 0u);
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kRegPa; }
    uint32_t MmioSize() const override { return kRegBytes; }

    uint32_t PanelFbOffset() const {
        return NormalizePanelFbOffset(panel_fb_raw_);
    }

    uint32_t PanelPitchBytes() const {
        return panel_pitch_bytes_ ? panel_pitch_bytes_ : kFbStride;
    }

    uint8_t ReadByte(uint32_t a) override {
        return static_cast<uint8_t>(ReadWord(a & ~3u) >> ((a & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t a) override {
        return static_cast<uint16_t>(ReadWord(a & ~3u) >> ((a & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t a) override {
        const uint32_t off = a - kRegPa;
        uint32_t value = 0u;
        switch (off) {
        case 0x000024u: value = 0x00180002u; break;
        case 0x000028u: value = 0u; break;
        case 0x00002Cu:
            value = g_mp377_smi_master_pending.load(std::memory_order_acquire) ? kMp377Sm501MasterSmiBit : 0u;
            break;
        case 0x000038u: value = 0x00021807u; break;
        case 0x00003Cu: value = 0x2A1A0A09u; break;
        case 0x000060u: value = 0x050100A0u; break;
        case 0x020008u: case 0x020108u: {
            value = siemens_mp377::Mp377TouchReadSmiSampleWord();
            break;
        }
        case 0x02000Cu: case 0x02010Cu: {
            /* smibase sub_2B53FDC completes a TX only when:
                   (SMI_INT_STATUS & 2) && (SMI_STATUS & 3).
               While the driver has ctrl bit 1 set, expose one TX-ready bit
               along with the stable status bits. */
            const uint32_t ctrl_off = (off == 0x02010Cu) ? 0x020104u : 0x020004u;
            const bool tx_active = (regs_[ctrl_off / 4u] & 0x2u) != 0u;
            value = tx_active ? 0x0000000Du : 0x0000000Cu;
            break;
        }
        case 0x020014u: case 0x020114u:
            /* smibase sub_2B53FDC treats INT_STATUS bit 0x4 as RX/FIFO error.
               Expose only the TX-completion bit here; ADC reads use the data
               register path, not the RX/error interrupt bit. */
            value = 0x00000002u;
            break;
        default:
            value = regs_[off / 4u];
            break;
        }
        return value;
    }
    void WriteByte(uint32_t a, uint8_t v) override {
        const uint32_t off = a - kRegPa;
        const uint32_t shift = (off & 3u) * 8u;
        uint32_t w = regs_[off / 4u];
        w = (w & ~(0xFFu << shift)) | (static_cast<uint32_t>(v) << shift);
        WriteWord(a & ~3u, w);
    }
    void WriteHalf(uint32_t a, uint16_t v) override {
        const uint32_t off = a - kRegPa;
        const uint32_t shift = (off & 2u) * 8u;
        uint32_t w = regs_[off / 4u];
        w = (w & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(v) << shift);
        WriteWord(a & ~3u, w);
    }
    void WriteWord(uint32_t a, uint32_t v) override {
        const uint32_t off = a - kRegPa;
        const uint32_t old_value = regs_[off / 4u];
        regs_[off / 4u] = v;

        if ((off == 0x020004u || off == 0x020104u) &&
            ((old_value & 0x2u) == 0u) && ((v & 0x2u) != 0u)) {
            g_mp377_smi_master_pending.store(1u, std::memory_order_release);
            g_mp377_smi_bridge_status.fetch_or(kMp377SmiBridgeStatusBit, std::memory_order_acq_rel);
            Mp377SmiAssertCascade();
        }

        if ((off == 0x020004u || off == 0x020104u) &&
            ((old_value & 0x2u) != 0u) && ((v & 0x2u) == 0u)) {
            /* smibase sub_2B53FDC completes TX by clearing CTRL bit 1 before
               setting tx_event.  Model that falling edge as hardware TX IRQ
               completion/ack as well, otherwise CP6 source 24 can remain
               pending and starve lower-priority touch IRQ 0x23. */
            Mp377SmiClearPending();
        }

        if (off == 0x00002Cu && (v & kMp377Sm501MasterSmiBit) != 0u) {
            Mp377SmiClearPending();
        }

        if ((off == 0x020014u || off == 0x020114u) && (v & 0x2u) != 0u) {
            Mp377SmiClearPending();
        }

        if (off == 0x080044u) {
            panel_fb_raw_ = v;
        } else if (off == 0x080048u) {
            panel_pitch_bytes_ = DecodePanelPitchBytes(v);
        }

        if (off == 0x020008u || off == 0x020108u) {
            siemens_mp377::Mp377QueueSmiCommand(static_cast<uint16_t>(v & 0xFFFFu));
        }
        if (off == 0x10000Cu && (v & 0xC0000000u))
            ExecuteDdiVgx2dCommand(v);
        if (off >= 0x110000u && off < 0x110100u)
            HandleDdiVgxDataPortWord(v);

    }

private:
    static uint32_t NormalizePanelFbOffset(uint32_t v) {
        if (v >= kFbPa && v < kFbPa + kFbBytes)
            return v - kFbPa;

        const uint32_t off = v & 0x03FFFFFFu;
        return off < kFbBytes ? off : 0u;
    }

    static uint32_t DecodePanelPitchBytes(uint32_t v) {
        uint32_t p = v & 0x3FFFu;
        if (!p) p = (v >> 16) & 0x3FFFu;
        if (p == kFbWidth) p *= 2u;
        if (p < kFbStride || p > kFbBytes / 2u) p = kFbStride;
        return p;
    }

    static uint32_t Lo16(uint32_t v) { return v & 0xFFFFu; }
    static uint32_t Hi16(uint32_t v) { return (v >> 16) & 0xFFFFu; }

    static constexpr uint32_t k2dSrcXY       = 0x100000u;
    static constexpr uint32_t k2dDstXY       = 0x100004u;
    static constexpr uint32_t k2dExtent      = 0x100008u;
    static constexpr uint32_t k2dPitchPair   = 0x100010u;
    static constexpr uint32_t k2dFgColor     = 0x100014u;
    static constexpr uint32_t k2dInvFg       = 0x100018u;
    static constexpr uint32_t k2dMonoFg      = 0x100020u;
    static constexpr uint32_t k2dBrush0      = 0x100034u;
    static constexpr uint32_t k2dPitchMirror = 0x10003Cu;
    static constexpr uint32_t k2dSrcBase     = 0x100040u;
    static constexpr uint32_t k2dDstBase     = 0x100044u;

    uint32_t R(uint32_t off) const { return regs_[off / 4u]; }
    static uint32_t PitchLo(uint32_t v) { return v & 0xFFFFu; }
    static uint32_t PitchHi(uint32_t v) { return (v >> 16) & 0xFFFFu; }

    struct DdiSurfaceState {
        uint32_t base = 0;
        uint32_t pitch_pixels = kFbWidth;
        uint32_t pitch_bytes = kFbStride;
    };

    struct DdiColorState {
        uint16_t rgb565 = 0;
        bool valid = false;
    };

    struct Ddi2dState {
        uint32_t src_x = 0, src_y = 0, dst_x = 0, dst_y = 0, width = 0, height = 0;
        uint32_t src_pitch = kFbWidth, dst_pitch = kFbWidth;
        DdiSurfaceState src_surface;
        DdiSurfaceState dst_surface;
        uint16_t fill_color = 0, inv_fg = 0;
        DdiColorState mono_fg_state;
        uint8_t rop = 0;
        bool backwards = false;
    };

    static constexpr uint32_t kDdiHostColorBpp = 16u;

    uint32_t DecodePitchCandidate(uint32_t p, uint32_t fallback) const {
        return (p >= 1u && p <= 8192u) ? p : fallback;
    }

    uint32_t DecodeDstPitchPixels() const {
        const uint32_t pp = R(k2dPitchPair);
        const uint32_t pm = R(k2dPitchMirror);
        uint32_t p = PitchHi(pp);
        if (!p) p = PitchHi(pm);
        if (!p) p = PitchLo(pp);
        if (!p) p = PitchLo(pm);
        return DecodePitchCandidate(p, kFbWidth);
    }

    uint32_t DecodeSrcPitchPixels() const {
        const uint32_t pp = R(k2dPitchPair);
        const uint32_t pm = R(k2dPitchMirror);
        uint32_t p = PitchLo(pp);
        if (!p) p = PitchLo(pm);
        if (!p) p = PitchHi(pp);
        if (!p) p = PitchHi(pm);
        return DecodePitchCandidate(p, DecodeDstPitchPixels());
    }

    DdiSurfaceState DecodeSurface(bool source, uint32_t cmd) const {
        DdiSurfaceState s;

        const uint32_t raw_base = R(source ? k2dSrcBase : k2dDstBase);
        s.base = NormalizeFbOffset(raw_base);
        s.pitch_pixels = source ? DecodeSrcPitchPixels() : DecodeDstPitchPixels();
        const bool mono = source && cmd == 0x8048810Cu;
        const uint32_t bpp = mono ? 1u : 16u;
        s.pitch_bytes = std::max<uint32_t>(1u, (s.pitch_pixels * bpp + 7u) / 8u);
        if (!mono && s.pitch_bytes < s.pitch_pixels * 2u)
            s.pitch_bytes = s.pitch_pixels * 2u;
        return s;
    }

    static bool Rop3Bit(uint8_t rop, bool p, bool s, bool d) {
        const unsigned idx = (p ? 4u : 0u) | (s ? 2u : 0u) | (d ? 1u : 0u);
        return ((rop >> idx) & 1u) != 0;
    }

    static bool RopDependsOnSource(uint8_t rop) {
        for (unsigned p = 0; p < 2; ++p)
            for (unsigned d = 0; d < 2; ++d)
                if (Rop3Bit(rop, p != 0, false, d != 0) !=
                    Rop3Bit(rop, p != 0, true,  d != 0))
                    return true;
        return false;
    }

    Ddi2dState DecodeDdi2dState(uint32_t cmd) const {
        const uint32_t src_xy = R(k2dSrcXY);
        const uint32_t dst_xy = R(k2dDstXY);
        const uint32_t extent = R(k2dExtent);
        Ddi2dState st;
        st.src_x = Hi16(src_xy);
        st.src_y = Lo16(src_xy);
        st.dst_x = Hi16(dst_xy);
        st.dst_y = Lo16(dst_xy);
        st.width = Hi16(extent);
        st.height = Lo16(extent);
        st.src_surface = DecodeSurface(true, cmd);
        st.dst_surface = DecodeSurface(false, cmd);
        st.src_pitch = st.src_surface.pitch_pixels;
        st.dst_pitch = st.dst_surface.pitch_pixels;
        st.mono_fg_state = DecodeDdiMonoColor();
        st.fill_color = DecodeDdiFillColor().rgb565;
        st.inv_fg = DecodeDdiColorRegister(k2dInvFg, true).rgb565;
        st.rop = static_cast<uint8_t>(cmd & 0xFFu);
        st.backwards = (cmd & 0x08000000u) != 0;
        return st;
    }

    uint32_t NormalizeFbOffset(uint32_t v) const {
        if (v >= kFbPa && v < kFbPa + kFbBytes)
            return v - kFbPa;
        const uint32_t off = v & 0x03FFFFFFu;
        return off < kFbBytes ? off : 0u;
    }

    void FillRect16(const Ddi2dState& st, uint16_t color) {
        auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
        if (!fb) return;

        uint8_t* vram = fb->MutableVramFor2d();
        if (!vram) return;

        const DdiSurfaceState& dst = st.dst_surface;

        const uint32_t surface_w = SurfaceWidthPixels16(dst);
        const uint32_t surface_h = SurfaceHeightRows(dst);
        if (surface_w == 0 || surface_h == 0) return;
        if (st.dst_x >= surface_w || st.dst_y >= surface_h) return;

        const uint32_t width = std::min(st.width, surface_w - st.dst_x);
        const uint32_t height = std::min(st.height, surface_h - st.dst_y);
        if (width == 0 || height == 0) return;

        const uint32_t stride = dst.pitch_bytes ? dst.pitch_bytes : st.dst_pitch * 2u;
        if (stride == 0) return;

        for (uint32_t y = 0; y < height; ++y) {
            const uint32_t row = dst.base + (st.dst_y + y) * stride + st.dst_x * 2u;
            if (row >= kFbBytes) break;

            const uint32_t row_bytes = std::min(width * 2u, kFbBytes - row);
            for (uint32_t x = 0; x + 1u < row_bytes; x += 2u) {
                const uint16_t d = static_cast<uint16_t>(vram[row + x] | (vram[row + x + 1u] << 8));
                const uint16_t out = ApplyDdiRop16(st.rop, color, d, color);
                vram[row + x] = static_cast<uint8_t>(out);
                vram[row + x + 1u] = static_cast<uint8_t>(out >> 8);
            }

            fb->Note2dWrite(row, row_bytes);
        }
    }

    static bool LooksLikeDdiPitchPair(uint32_t v) {
        const uint32_t lo = v & 0xFFFFu;
        const uint32_t hi = (v >> 16) & 0xFFFFu;
        if (lo == 0 || hi == 0) return false;
        if (lo != hi) return false;
        return lo >= 16u && lo <= 8192u;
    }

    DdiColorState DecodeDdiColorRegister(uint32_t off, bool allow_pitch_like) const {
        const uint32_t raw = R(off);
        if (!allow_pitch_like && LooksLikeDdiPitchPair(raw)) return {};
        return { static_cast<uint16_t>(raw & 0xFFFFu), true };
    }

    DdiColorState FirstValidColor(DdiColorState a, DdiColorState b, DdiColorState c,
                                  uint16_t fallback) const {
        if (a.valid) return a;
        if (b.valid) return b;
        if (c.valid) return c;
        return { fallback, true };
    }

    DdiColorState DecodeDdiFillColor() const {
        return FirstValidColor(DecodeDdiColorRegister(k2dFgColor, false),
                               DecodeDdiColorRegister(k2dMonoFg, false),
                               DecodeDdiColorRegister(k2dBrush0, false),
                               0x0000u);
    }

    DdiColorState DecodeDdiMonoColor() const {
        return FirstValidColor(DecodeDdiColorRegister(k2dMonoFg, false),
                               DecodeDdiColorRegister(k2dFgColor, false),
                               {},
                               0xFFFFu);
    }

    bool IsDdiVgxHostDataCommand(uint32_t cmd) const {

        if (cmd == 0x8048810Cu) return true;
        if ((cmd & 0xFFFF0000u) == 0x80080000u) return true;
        return false;
    }

    void BeginDdiVgxHostDataCommand(uint32_t cmd) {
        host_data_active_ = true;
        host_data_mono_ = (cmd == 0x8048810Cu);

        const uint32_t dst_xy = regs_[0x100004u / 4u];
        const uint32_t wh = regs_[0x100008u / 4u];

        host_dst_x_ = Hi16(dst_xy);
        host_dst_y_ = Lo16(dst_xy);
        host_width_ = Hi16(wh);
        host_height_ = Lo16(wh);

        if (host_width_ == 0) host_width_ = 1;
        if (host_height_ == 0) host_height_ = 1;
        if (host_width_ > 2048u) host_width_ = 2048u;
        if (host_height_ > 2048u) host_height_ = 2048u;

        const Ddi2dState st = DecodeDdi2dState(cmd);
        host_dst_pitch_bytes_ = st.dst_surface.pitch_bytes;
        host_dst_surface_width_ = SurfaceWidthPixels16(st.dst_surface);
        host_dst_surface_height_ = SurfaceHeightRows(st.dst_surface);
        host_dst_base_ = st.dst_surface.base;
        host_fg_ = st.mono_fg_state.rgb565;
        host_bg_ = st.inv_fg;
        host_mono_transparent_ = (cmd == 0x8048810Cu);
        host_y_ = 0;
        host_src_byte_in_row_ = 0;
        host_src_bit_offset_ = 0;
        host_row_bytes_.clear();

        if (host_data_mono_) {

            const uint32_t src_field = R(k2dSrcXY);
            host_src_bit_offset_ = (src_field >> 16) & 7u;
            const uint32_t active_bits = host_src_bit_offset_ + host_width_;
            host_src_active_bytes_ = (active_bits + 7u) / 8u;
            host_src_pitch_bytes_ = (host_src_active_bytes_ + 3u) & ~3u;
        } else {
            host_src_bit_offset_ = 0;
            host_src_active_bytes_ = (host_width_ * kDdiHostColorBpp + 7u) / 8u;
            host_src_pitch_bytes_ = (host_src_active_bytes_ + 7u) & ~7u;
            host_row_bytes_.clear();
            host_row_bytes_.reserve(host_src_pitch_bytes_);
        }
    }

    void CompleteHostDataIfDone() {
        if (!host_data_active_) return;
        if (host_y_ >= host_height_) {
            host_data_active_ = false;
            host_row_bytes_.clear();
        }
    }

    void HostDataWritePixel(uint32_t x, uint32_t y, uint16_t p) {
        auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
        if (!fb) return;

        const uint32_t abs_x = host_dst_x_ + x;
        const uint32_t abs_y = host_dst_y_ + y;
        if (host_dst_surface_width_ && abs_x >= host_dst_surface_width_) return;
        if (host_dst_surface_height_ && abs_y >= host_dst_surface_height_) return;

        uint8_t* vram = fb->MutableVramFor2d();
        if (!vram) return;

        const uint32_t stride = host_dst_pitch_bytes_;
        const uint32_t off = host_dst_base_ + abs_y * stride + abs_x * 2u;
        if (off + 1u >= kFbBytes) return;

        vram[off] = static_cast<uint8_t>(p);
        vram[off + 1u] = static_cast<uint8_t>(p >> 8);
        fb->Note2dWrite(off, 2u);
    }

    void HostDataAdvanceRow() {
        host_src_byte_in_row_ = 0;
        ++host_y_;
        host_row_bytes_.clear();
        CompleteHostDataIfDone();
    }

    void HostDataMonoByte(uint8_t b) {
        if (!host_data_active_) return;

        if (host_src_byte_in_row_ < host_src_active_bytes_ && host_y_ < host_height_) {

            for (int bit = 7; bit >= 0; --bit) {
                const uint32_t bit_in_row = host_src_byte_in_row_ * 8u + static_cast<uint32_t>(7 - bit);
                if (bit_in_row < host_src_bit_offset_) continue;

                const uint32_t px = bit_in_row - host_src_bit_offset_;
                if (px >= host_width_) continue;

                if (b & (1u << bit)) {
                    HostDataWritePixel(px, host_y_, host_fg_);
                } else if (!host_mono_transparent_) {
                    HostDataWritePixel(px, host_y_, host_bg_);
                }
            }
        }

        ++host_src_byte_in_row_;
        if (host_src_byte_in_row_ >= host_src_pitch_bytes_) {
            HostDataAdvanceRow();
        }
    }

    uint8_t HostRowByte(uint32_t i) const {
        return i < host_row_bytes_.size() ? host_row_bytes_[i] : 0;
    }

    void FlushHostDataColorRow() {
        if (host_y_ >= host_height_) return;

        for (uint32_t x = 0; x < host_width_; ++x) {
            const uint32_t i = x * 2u;
            const uint16_t p = static_cast<uint16_t>(HostRowByte(i) | (HostRowByte(i + 1u) << 8));
            HostDataWritePixel(x, host_y_, p);
        }
    }

    void HostDataColorByte(uint8_t b) {
        if (!host_data_active_) return;

        if (host_src_byte_in_row_ < host_src_active_bytes_ && host_y_ < host_height_)
            host_row_bytes_.push_back(b);

        ++host_src_byte_in_row_;
        if (host_src_byte_in_row_ >= host_src_pitch_bytes_) {
            FlushHostDataColorRow();
            HostDataAdvanceRow();
        }
    }

    void BeginDdiPatternUpload() {
        pattern_upload_active_ = true;
        pattern_valid_ = false;
        pattern_words_.clear();
    }

    void FinishDdiPatternUpload() {
        if (!pattern_upload_active_) return;
        pattern_upload_active_ = false;
        pattern_valid_ = !pattern_words_.empty();

    }

    void HandleDdiPatternDataPortWord(uint32_t v) {
        if (!pattern_upload_active_) return;
        if (pattern_words_.size() < 128u) pattern_words_.push_back(v);
    }

    uint16_t PatternPixel565(uint32_t x, uint32_t y, uint16_t fallback) const {
        if (!pattern_valid_ || pattern_words_.empty()) return fallback;
        const uint32_t px = (x & 7u);
        const uint32_t py = (y & 7u);
        const uint32_t i = py * 8u + px;

        if (pattern_words_.size() >= 32u && pattern_words_.size() < 64u) {
            const uint32_t w = pattern_words_[i / 2u];
            return static_cast<uint16_t>((i & 1u) ? (w >> 16) : (w & 0xFFFFu));
        }
        if (pattern_words_.size() >= 64u) {
            const uint32_t c = pattern_words_[i];
            const uint8_t r = static_cast<uint8_t>((c >> 16) & 0xFFu);
            const uint8_t g = static_cast<uint8_t>((c >> 8) & 0xFFu);
            const uint8_t b = static_cast<uint8_t>(c & 0xFFu);
            return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
        }
        if (pattern_words_.size() >= 16u) {
            const uint32_t w = pattern_words_[i / 4u];
            const uint8_t idx = static_cast<uint8_t>((w >> ((i & 3u) * 8u)) & 0xFFu);

            return static_cast<uint16_t>(((idx & 0xF8u) << 8) | ((idx & 0xFCu) << 3) | (idx >> 3));
        }
        return fallback;
    }

    void PatternFillRect16(const Ddi2dState& st, uint16_t fallback) {
        auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
        if (!fb) return;
        uint8_t* vram = fb->MutableVramFor2d();
        if (!vram) return;

        const DdiSurfaceState& dst = st.dst_surface;

        const uint32_t surface_w = SurfaceWidthPixels16(dst);
        const uint32_t surface_h = SurfaceHeightRows(dst);
        if (surface_w == 0 || surface_h == 0) return;
        if (st.dst_x >= surface_w || st.dst_y >= surface_h) return;

        const uint32_t width = std::min(st.width, surface_w - st.dst_x);
        const uint32_t height = std::min(st.height, surface_h - st.dst_y);
        if (width == 0 || height == 0) return;

        const uint32_t stride = dst.pitch_bytes ? dst.pitch_bytes : st.dst_pitch * 2u;
        if (stride == 0) return;

        for (uint32_t y = 0; y < height; ++y) {
            const uint32_t row = dst.base + (st.dst_y + y) * stride + st.dst_x * 2u;
            if (row >= kFbBytes) break;
            const uint32_t row_bytes = std::min(width * 2u, kFbBytes - row);
            for (uint32_t x = 0; x + 1u < row_bytes; x += 2u) {
                const uint32_t px = x >> 1;
                const uint16_t color = PatternPixel565(px, y, fallback);
                const uint16_t d = static_cast<uint16_t>(vram[row + x] | (vram[row + x + 1u] << 8));
                const uint16_t out = ApplyDdiRop16(st.rop, color, d, color);
                vram[row + x] = static_cast<uint8_t>(out);
                vram[row + x + 1u] = static_cast<uint8_t>(out >> 8);
            }
            fb->Note2dWrite(row, row_bytes);
        }
    }

    void HandleDdiVgxDataPortWord(uint32_t v) {
        if (pattern_upload_active_) {
            HandleDdiPatternDataPortWord(v);
            return;
        }
        if (!host_data_active_) return;

        const uint8_t b0 = static_cast<uint8_t>(v);
        const uint8_t b1 = static_cast<uint8_t>(v >> 8);
        const uint8_t b2 = static_cast<uint8_t>(v >> 16);
        const uint8_t b3 = static_cast<uint8_t>(v >> 24);

        if (host_data_mono_) {
            HostDataMonoByte(b0);
            HostDataMonoByte(b1);
            HostDataMonoByte(b2);
            HostDataMonoByte(b3);
        } else {
            HostDataColorByte(b0);
            HostDataColorByte(b1);
            HostDataColorByte(b2);
            HostDataColorByte(b3);
        }
    }

    void ExecuteDdiFill(const Ddi2dState& st, bool use_pattern) {
        if (use_pattern && pattern_valid_)
            PatternFillRect16(st, st.fill_color);
        else
            FillRect16(st, st.fill_color);
    }

    uint32_t SurfaceWidthPixels16(const DdiSurfaceState& s) const {
        if (s.base == 0u) return kFbWidth;
        if (s.pitch_pixels) return s.pitch_pixels;
        return s.pitch_bytes >= 2u ? s.pitch_bytes / 2u : kFbWidth;
    }

    uint32_t SurfaceHeightRows(const DdiSurfaceState& s) const {
        if (s.base == 0u) return kFbHeight;
        if (s.base >= kFbBytes || s.pitch_bytes == 0) return 0;
        return (kFbBytes - s.base) / s.pitch_bytes;
    }

    static uint16_t ApplyDdiRop16(uint8_t rop, uint16_t s, uint16_t d, uint16_t p = 0xFFFFu) {

        uint16_t out = 0;
        for (unsigned bit = 0; bit < 16; ++bit) {
            const bool pb = ((p >> bit) & 1u) != 0;
            const bool sb = ((s >> bit) & 1u) != 0;
            const bool db = ((d >> bit) & 1u) != 0;
            if (Rop3Bit(rop, pb, sb, db)) out |= static_cast<uint16_t>(1u << bit);
        }
        return out;
    }

    struct DdiVramBlitRect {
        uint32_t src_x = 0, src_y = 0, dst_x = 0, dst_y = 0;
        uint32_t width = 0, height = 0;
        bool rtl_btl = false;
    };

    DdiVramBlitRect NormalizeDdiVramBlitRect(const Ddi2dState& st) const {
        DdiVramBlitRect r;
        r.src_x = st.src_x;
        r.src_y = st.src_y;
        r.dst_x = st.dst_x;
        r.dst_y = st.dst_y;
        r.width = st.width;
        r.height = st.height;
        r.rtl_btl = st.backwards;

        if (r.rtl_btl) {
            if (r.width) {
                r.src_x = (r.src_x + 1u >= r.width) ? (r.src_x - r.width + 1u) : 0u;
                r.dst_x = (r.dst_x + 1u >= r.width) ? (r.dst_x - r.width + 1u) : 0u;
            }
            if (r.height) {
                r.src_y = (r.src_y + 1u >= r.height) ? (r.src_y - r.height + 1u) : 0u;
                r.dst_y = (r.dst_y + 1u >= r.height) ? (r.dst_y - r.height + 1u) : 0u;
            }
        }

        const uint32_t src_w = SurfaceWidthPixels16(st.src_surface);
        const uint32_t dst_w = SurfaceWidthPixels16(st.dst_surface);
        const uint32_t src_h = SurfaceHeightRows(st.src_surface);
        const uint32_t dst_h = SurfaceHeightRows(st.dst_surface);

        if (r.src_x >= src_w || r.src_y >= src_h || r.dst_x >= dst_w || r.dst_y >= dst_h) {
            r.width = 0;
            r.height = 0;
            return r;
        }

        r.width = std::min(r.width, std::min(src_w - r.src_x, dst_w - r.dst_x));
        r.height = std::min(r.height, std::min(src_h - r.src_y, dst_h - r.dst_y));
        return r;
    }

    void ExecuteDdiVideoToVideoChunk(const Ddi2dState& st, const DdiVramBlitRect& r,
                                     uint32_t y_off, uint32_t rows) {
        auto* fb = emu_.TryGet<SiemensMp377Sm501Fb>();
        if (!fb || rows == 0 || r.width == 0) return;

        uint8_t* vram = fb->MutableVramFor2d();
        if (!vram) return;

        const uint32_t src_stride = st.src_surface.pitch_bytes ? st.src_surface.pitch_bytes : st.src_pitch * 2u;
        const uint32_t dst_stride = st.dst_surface.pitch_bytes ? st.dst_surface.pitch_bytes : st.dst_pitch * 2u;

        const uint32_t dst_x = r.dst_x;

        std::vector<uint16_t> src_tmp(r.width * rows, 0u);
        for (uint32_t y = 0; y < rows; ++y) {
            const uint32_t src_row = st.src_surface.base + (r.src_y + y_off + y) * src_stride + r.src_x * 2u;
            for (uint32_t x = 0; x < r.width; ++x) {
                const uint32_t off = src_row + x * 2u;
                if (off + 1u < kFbBytes)
                    src_tmp[y * r.width + x] = static_cast<uint16_t>(vram[off] | (vram[off + 1u] << 8));
            }
        }

        for (uint32_t y = 0; y < rows; ++y) {
            const uint32_t dst_row = st.dst_surface.base + (r.dst_y + y_off + y) * dst_stride + dst_x * 2u;
            if (dst_row >= kFbBytes) break;
            for (uint32_t x = 0; x < r.width; ++x) {
                const uint32_t off = dst_row + x * 2u;
                if (off + 1u >= kFbBytes) break;
                const uint16_t src = src_tmp[y * r.width + x];
                const uint16_t d = static_cast<uint16_t>(vram[off] | (vram[off + 1u] << 8));
                const uint16_t o = ApplyDdiRop16(st.rop, src, d, st.fill_color);
                vram[off] = static_cast<uint8_t>(o);
                vram[off + 1u] = static_cast<uint8_t>(o >> 8);
            }
            fb->Note2dWrite(dst_row, r.width * 2u);
        }
    }

    void ExecuteDdiVideoToVideo(const Ddi2dState& st) {
        DdiVramBlitRect r = NormalizeDdiVramBlitRect(st);
        if (r.width == 0 || r.height == 0) {
            return;
        }

        constexpr uint32_t kDdiVramBlitMaxRows = 0xC0u;
        for (uint32_t y = 0; y < r.height; ) {
            const uint32_t rows = std::min<uint32_t>(kDdiVramBlitMaxRows, r.height - y);
            ExecuteDdiVideoToVideoChunk(st, r, y, rows);
            y += rows;
        }
    }

    void ExecuteDdiVgx2dCommand(uint32_t cmd) {
        if (cmd == 0x40000000u) {
            BeginDdiPatternUpload();
            return;
        }

        FinishDdiPatternUpload();

        if (IsDdiVgxHostDataCommand(cmd)) {
            BeginDdiVgxHostDataCommand(cmd);
            return;
        }

        const Ddi2dState st = DecodeDdi2dState(cmd);
        const uint32_t width = st.width;
        const uint32_t height = st.height;

        if (width == 0 || height == 0) {
            return;
        }

        const uint8_t rop = static_cast<uint8_t>(cmd & 0xFFu);
        if ((cmd & 0x40000000u) != 0)
            ExecuteDdiFill(st, true);
        else if ((cmd & 0x0C000000u) != 0 || RopDependsOnSource(rop))
            ExecuteDdiVideoToVideo(st);
        else
            ExecuteDdiFill(st, false);
    }

    std::vector<uint32_t> regs_;
    uint32_t panel_fb_raw_ = 0u;
    uint32_t panel_pitch_bytes_ = kFbStride;

    bool pattern_upload_active_ = false;
    bool pattern_valid_ = false;
    std::vector<uint32_t> pattern_words_;

    bool host_data_active_ = false;
    bool host_data_mono_ = false;
    uint32_t host_dst_x_ = 0;
    uint32_t host_dst_y_ = 0;
    uint32_t host_width_ = 0;
    uint32_t host_height_ = 0;
    uint32_t host_dst_pitch_bytes_ = kFbStride;
    uint32_t host_dst_surface_width_ = kFbWidth;
    uint32_t host_dst_surface_height_ = kFbHeight;
    uint32_t host_dst_base_ = 0;
    uint32_t host_y_ = 0;
    uint32_t host_src_byte_in_row_ = 0;
    uint32_t host_src_bit_offset_ = 0;
    uint32_t host_src_active_bytes_ = 0;
    uint32_t host_src_pitch_bytes_ = 0;
    std::vector<uint8_t> host_row_bytes_;
    uint16_t host_fg_ = 0xFFFFu;
    uint16_t host_bg_ = 0x0000u;
    bool host_mono_transparent_ = true;
};

} // namespace

namespace siemens_mp377 {

void Mp377SmiBindEmulator(CerfEmulator* emu) {
    g_mp377_smi_emu.store(emu, std::memory_order_release);
}

uint32_t Mp377SmiBridgeRead(uint32_t pa) {
    const uint32_t a = pa & ~3u;
    const uint32_t base = (a >= 0xC4800028u && a <= 0xC4800034u) ? 0xC4800028u :
                          (a >= 0xC4100028u && a <= 0xC4100034u) ? 0xC4100028u : 0u;
    if (!base) return 0u;

    switch (a - base) {
    case 0x04u:
        return g_mp377_smi_bridge_enable.load(std::memory_order_acquire);
    case 0x08u:
        return g_mp377_smi_bridge_status.load(std::memory_order_acquire);
    default:
        return 0u;
    }
}

void Mp377SmiBridgeWrite(uint32_t pa, uint32_t value) {
    const uint32_t a = pa & ~3u;
    const uint32_t base = (a >= 0xC4800028u && a <= 0xC4800034u) ? 0xC4800028u :
                          (a >= 0xC4100028u && a <= 0xC4100034u) ? 0xC4100028u : 0u;
    if (!base) return;

    switch (a - base) {
    case 0x04u:
        g_mp377_smi_bridge_enable.store(value, std::memory_order_release);
        break;
    case 0x08u:
        /* Boot-time wide fills write 0xFFFFFFFF across C4100000..; do not turn
           that into a synthetic SMI. Real OAL disable/ack writes the filtered
           live status value, normally zero for raw IRQ 0x83. */
        if (value == 0xFFFFFFFFu) return;

        g_mp377_smi_bridge_status.store(value & (kMp377SmiBridgeStatusBit | 0x400u),
                                        std::memory_order_release);
        if ((value & (kMp377SmiBridgeStatusBit | 0x400u)) == 0u)
            Mp377SmiClearPending();
        break;
    default:
        break;
    }
}

const uint8_t* Sm501Vram(CerfEmulator& emu) {
    auto* fb = emu.TryGet<SiemensMp377Sm501Fb>();
    return fb ? fb->Vram() : nullptr;
}

bool Sm501WasWritten(CerfEmulator& emu) {
    auto* fb = emu.TryGet<SiemensMp377Sm501Fb>();
    return fb && fb->WasWritten();
}

uint32_t Sm501PanelFbOffset(CerfEmulator& emu) {
    auto* regs = emu.TryGet<SiemensMp377Sm501Regs>();
    return regs ? regs->PanelFbOffset() : 0u;
}

uint32_t Sm501PanelPitchBytes(CerfEmulator& emu) {
    auto* regs = emu.TryGet<SiemensMp377Sm501Regs>();
    return regs ? regs->PanelPitchBytes() : kFbStride;
}

} // namespace siemens_mp377

REGISTER_SERVICE(SiemensMp377Sm501Fb);
REGISTER_SERVICE(SiemensMp377Sm501Regs);
