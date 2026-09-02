#pragma once

#include "siemens_mp377_sm501.h"

#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../core/service.h"
#include "../../state/state_stream.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace siemens_mp377 {

class SiemensMp377Sm501Regs;

/* SM501 datasheet Table 4-1: 2D engine at BAR1+0x100000 and its data-port
   aperture at BAR1+0x110000. */
class SiemensMp377Sm501Blitter : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void ExecuteCommand(uint32_t command);
    void WriteDataPort(uint32_t value);
    static bool IsCommandRegister(uint32_t offset) { return offset == 0x10000Cu; }
    static bool IsDataPort(uint32_t offset) { return offset >= 0x110000u && offset < 0x110100u; }

    void SaveState(StateWriter& w) const {
        w.Write(pattern_upload_active_);
        w.Write(pattern_valid_);
        WriteVectorState(w, pattern_words_);
        w.Write(host_data_active_);
        w.Write(host_data_mono_);
        w.Write(host_dst_x_);
        w.Write(host_dst_y_);
        w.Write(host_width_);
        w.Write(host_height_);
        w.Write(host_dst_pitch_bytes_);
        w.Write(host_dst_surface_width_);
        w.Write(host_dst_surface_height_);
        w.Write(host_dst_base_);
        w.Write(host_y_);
        w.Write(host_src_byte_in_row_);
        w.Write(host_src_bit_offset_);
        w.Write(host_src_active_bytes_);
        w.Write(host_src_pitch_bytes_);
        WriteVectorState(w, host_row_bytes_);
        w.Write(host_fg_);
        w.Write(host_bg_);
        w.Write(host_mono_transparent_);
    }

    void RestoreState(StateReader& r) {
        r.Read(pattern_upload_active_);
        r.Read(pattern_valid_);
        ReadVectorState(r, pattern_words_, 1024u, "SM501 pattern state size");
        r.Read(host_data_active_);
        r.Read(host_data_mono_);
        r.Read(host_dst_x_);
        r.Read(host_dst_y_);
        r.Read(host_width_);
        r.Read(host_height_);
        r.Read(host_dst_pitch_bytes_);
        r.Read(host_dst_surface_width_);
        r.Read(host_dst_surface_height_);
        r.Read(host_dst_base_);
        r.Read(host_y_);
        r.Read(host_src_byte_in_row_);
        r.Read(host_src_bit_offset_);
        r.Read(host_src_active_bytes_);
        r.Read(host_src_pitch_bytes_);
        ReadVectorState(r, host_row_bytes_, 4096u, "SM501 host row state size");
        r.Read(host_fg_);
        r.Read(host_bg_);
        r.Read(host_mono_transparent_);
    }

private:
    struct SurfaceState {
        uint32_t base = 0;
        uint32_t pitch_pixels = kFbWidth;
        uint32_t pitch_bytes = kFbStride;
    };
    struct ColorState {
        uint16_t rgb565 = 0;
        bool valid = false;
    };
    struct State2d {
        uint32_t src_x = 0, src_y = 0, dst_x = 0, dst_y = 0, width = 0, height = 0;
        uint32_t src_pitch = kFbWidth, dst_pitch = kFbWidth;
        SurfaceState src_surface;
        SurfaceState dst_surface;
        uint16_t fill_color = 0, inv_fg = 0;
        ColorState mono_fg_state;
        uint8_t rop = 0;
        bool backwards = false;
    };
    struct VramBlitRect {
        uint32_t src_x = 0, src_y = 0, dst_x = 0, dst_y = 0;
        uint32_t width = 0, height = 0;
        bool rtl_btl = false;
    };

    template <typename T> static void WriteVectorState(StateWriter& w, const std::vector<T>& v) {
        const uint64_t n = static_cast<uint64_t>(v.size());
        w.Write(n);
        if (n) w.WriteBytes(v.data(), static_cast<size_t>(n * sizeof(T)));
    }
    template <typename T>
    void ReadVectorState(StateReader& r, std::vector<T>& v, size_t max_expected, const char* what) {
        uint64_t n = 0;
        r.Read(n);
        if (n > static_cast<uint64_t>(max_expected)) {
            emu_.template Get<Fatal>().Die("%s: count=%llu maximum=%zu", what, static_cast<unsigned long long>(n),
                                           max_expected);
        }
        v.resize(static_cast<size_t>(n));
        if (n) r.ReadBytes(v.data(), static_cast<size_t>(n * sizeof(T)));
    }

    static constexpr uint32_t k2dSource = 0x100000u;
    static constexpr uint32_t k2dDestination = 0x100004u;
    static constexpr uint32_t k2dDimension = 0x100008u;
    static constexpr uint32_t k2dPitch = 0x100010u;
    static constexpr uint32_t k2dForeground = 0x100014u;
    static constexpr uint32_t k2dBackground = 0x100018u;
    static constexpr uint32_t k2dSourceBase = 0x100040u;
    static constexpr uint32_t k2dDestinationBase = 0x100044u;
    static constexpr uint32_t k2dPitchMask = 0x1FFFu;
    static constexpr uint32_t kDdiHostColorBpp = 16u;
    static constexpr uint32_t kDdiHostMonoTransparentCommand = 0x8048810Cu;
    static constexpr uint32_t kDdiHostColorCommandMask = 0xFFFF0000u;
    static constexpr uint32_t kDdiHostColorCommandBase = 0x80080000u;

    uint32_t R(uint32_t off) const;
    static uint32_t SrcPitchField(uint32_t v);
    static uint32_t DstPitchField(uint32_t v);
    static uint32_t Lo16(uint32_t v);
    static uint32_t Hi16(uint32_t v);
    void Log2dCommand(uint32_t, const State2d&, const char*);
    uint32_t DecodePitchField(uint32_t p, uint32_t fallback) const;
    uint32_t DecodeDstPitchPixels() const;
    uint32_t DecodeSrcPitchPixels() const;
    SurfaceState DecodeSurface(bool source, uint32_t cmd) const;
    static bool Rop3Bit(uint8_t rop, bool p, bool s, bool d);
    static bool RopDependsOnSource(uint8_t rop);
    State2d DecodeState2d(uint32_t cmd) const;
    uint32_t NormalizeFbOffset(uint32_t v) const;
    void FillRect16(const State2d& st, uint16_t color);
    ColorState DecodeDdiColorRegister(uint32_t off) const;
    ColorState DecodeDdiFillColor() const;
    ColorState DecodeDdiMonoColor() const;
    bool IsDdiVgxHostDataCommand(uint32_t cmd) const;
    void BeginDdiVgxHostDataCommand(uint32_t cmd);
    void CompleteHostDataIfDone();
    void HostDataWritePixel(uint32_t x, uint32_t y, uint16_t p);
    void HostDataAdvanceRow();
    void HostDataMonoByte(uint8_t b);
    uint8_t HostRowByte(uint32_t i) const;
    void FlushHostDataColorRow();
    void HostDataColorByte(uint8_t b);
    void BeginDdiPatternUpload();
    void FinishDdiPatternUpload();
    void HandleDdiPatternDataPortWord(uint32_t v);
    uint16_t PatternPixel565(uint32_t x, uint32_t y, uint16_t fallback) const;
    void PatternFillRect16(const State2d& st, uint16_t fallback);
    void HandleDdiVgxDataPortWord(uint32_t v);
    void ExecuteDdiFill(const State2d& st, bool use_pattern);
    uint32_t SurfaceWidthPixels16(const SurfaceState& s) const;
    uint32_t SurfaceHeightRows(const SurfaceState& s) const;
    static uint16_t ApplyDdiRop16(uint8_t rop, uint16_t s, uint16_t d, uint16_t p = 0xFFFFu);
    VramBlitRect NormalizeVramBlitRect(const State2d& st) const;
    void ExecuteDdiVideoToVideoChunk(const State2d& st, const VramBlitRect& r, uint32_t y_off, uint32_t rows);
    void ExecuteDdiVideoToVideo(const State2d& st);
    void ExecuteDdiVgx2dCommand(uint32_t cmd);

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

} // namespace siemens_mp377
