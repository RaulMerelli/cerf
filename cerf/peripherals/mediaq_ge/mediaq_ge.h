#pragma once

#include <cstdint>
#include <vector>

/* Shared MediaQ 2D GE, MQ-1100/1132 + MQ-200 (MQ-200 Data Book Table 5-63..5-74;
   MQ-1132 datasheet Reg 4-83..4-98). GE00R command + operands are family-uniform
   (here); colour register indices, field widths, solid-fill encoding, Source-FIFO
   packing are per-part (virtual) — split them wrong and the other part draws garbage. */

/* The owning peripheral exposes its framebuffer to the engine. */
class MediaQGeHost {
public:
    virtual ~MediaQGeHost() = default;
    virtual uint8_t* FbMutableBytes() = 0;
    virtual uint32_t FbSize() const = 0;
};

class MediaQGe {
public:
    explicit MediaQGe(MediaQGeHost& host) : host_(host) {}
    virtual ~MediaQGe() = default;

    /* g = GE register dword index within the GE block. g == 0 (GE00R Drawing
       Command) latches the command and executes it (unless auto-execute). */
    void     WriteReg(uint32_t g, uint32_t value);
    uint32_t ReadReg(uint32_t g) const { return g < kNumRegs ? reg_[g] : 0u; }

    /* Source FIFO data port. A latched system-source blit draws once its full
       source has streamed in. */
    void PushSourceFifo(uint32_t value);

    /* Draw a latched blit on a GE-idle status read, in case its source
       under-delivers relative to the predicted dword count. */
    void FlushPending();

    /* CC01R Source/Command FIFO + GE status, read by the driver before/after
       each command. Synchronous execution => always reported empty/idle. */
    virtual uint32_t StatusReady() const = 0;

protected:
    static constexpr uint32_t kNumRegs = 0x80u;

    /* GE register dword indices shared by the family (offset = base + g*4). */
    enum : uint32_t {
        kGe00Command   = 0x00,
        kGe01Size      = 0x01,
        kGe02DstXY     = 0x02,
        kGe03SrcXY     = 0x03,
        kGe04ColorCmp  = 0x04,
        kGe05ClipLT    = 0x05,
        kGe06ClipRB    = 0x06,
        kGe0ADstStride = 0x0A,
        kGe0BBase      = 0x0B,
    };

    /* GE00R Drawing Command bit fields (MQ-200 Data Book Table 5-63). */
    static constexpr uint32_t kCmdRopMask    = 0x000000FFu; /* [7:0]  raster op. */
    static constexpr uint32_t kCmdTypeShift  = 8u;          /* [10:8] command type. */
    static constexpr uint32_t kCmdTypeMask   = 0x7u;
    static constexpr uint32_t kTypeNop       = 0x0u;        /* 000 = No Operation. */
    static constexpr uint32_t kTypeBitBlt    = 0x2u;        /* 010 = BitBLT. */
    static constexpr uint32_t kTypeLine      = 0x4u;        /* 100 = Bresenham line. */
    static constexpr uint32_t kCmdXNeg       = 1u << 11;    /* X direction negative. */
    static constexpr uint32_t kCmdYNeg       = 1u << 12;    /* Y direction negative. */
    static constexpr uint32_t kCmdSrcSystem  = 1u << 13;    /* source in system memory (Source FIFO). */
    static constexpr uint32_t kCmdMonoSrc    = 1u << 14;    /* monochrome source. */
    static constexpr uint32_t kCmdMonoPat    = 1u << 15;    /* monochrome pattern. */
    static constexpr uint32_t kCmdColorTrans = 1u << 16;    /* colour transparency enable. */
    static constexpr uint32_t kCmdColorTrPol = 1u << 17;    /* colour transparency polarity. */
    static constexpr uint32_t kCmdMonoTrans  = 1u << 18;    /* mono transparency enable. */
    static constexpr uint32_t kCmdMonoTrPol  = 1u << 19;    /* mono transparency polarity. */
    static constexpr uint32_t kCmdSolidSrc   = 1u << 23;    /* solid source/pattern = foreground colour. */
    static constexpr uint32_t kCmdRop2       = 1u << 25;    /* ROP2: [3:0] duplicated to [7:4]. */
    static constexpr uint32_t kCmdEnClip     = 1u << 26;    /* clipping enable. */

    /* Per-part register layout (the indices/field-widths/encodings that differ
       between MQ-1132 and MQ-200). */
    struct Layout {
        uint32_t fg_index;       /* solid-source / mono-source foreground colour. */
        uint32_t bg_index;       /* mono-source background colour. */
        uint32_t pat_fg_index;   /* solid-pattern / fill colour. */
        uint32_t src_stride_idx; /* GE09R: source stride / pack-mode. */
        uint32_t stride_mask;    /* GE0AR destination line-stride field. */
        uint32_t base_mask;      /* GE0BR base-address field. */
    };

    /* Part-specific hooks. */
    virtual const Layout& Lyt() const = 0;
    virtual bool          IsSolidFill(uint32_t cmd) const = 0;   /* this command is a flat fill. */
    virtual uint32_t      SolidFillColor() const = 0;            /* its source colour. */
    virtual uint32_t      LineColor(uint32_t cmd) const = 0;     /* solid colour for a Bresenham line. */
    virtual uint32_t      ExpectedSourceDwords() const = 0;      /* dwords a system-source blit consumes. */
    virtual void          BlitColorSource(const uint32_t* r) = 0;/* colour BitBLT, source via FIFO. */
    virtual void          BlitMonoSource(const uint32_t* r) = 0; /* mono->colour BitBLT, source via FIFO. */

    /* Destination colour depth, GE0AR[31:30]: 00=8bpp, 01=16bpp, 11=32bpp. */
    uint32_t BytesPerPixel() const;
    uint32_t DestStrideBytes() const { return reg_[kGe0ADstStride] & Lyt().stride_mask; }
    uint32_t BaseAddr()        const { return reg_[kGe0BBase] & Lyt().base_mask; }

    /* 3-operand raster op: result bit = rop[(P<<2)|(S<<1)|D], bitwise. */
    static uint32_t Rop3(uint8_t rop, uint32_t p, uint32_t s, uint32_t d);

    uint8_t*      Fb()     { return host_.FbMutableBytes(); }
    uint32_t      FbBytes() const { return host_.FbSize(); }
    const uint32_t* Regs() const { return reg_; }

    uint32_t reg_[kNumRegs] = {};
    std::vector<uint32_t> src_fifo_;

private:
    void Execute();
    void ExecutePending();
    void FillSolid(uint8_t rop, uint32_t color, uint32_t cmd);
    void DrawLine(uint32_t cmd);
    void BlitColorFromDisplay(uint32_t cmd);
    void BlitMonoFromDisplay(uint32_t cmd);

    MediaQGeHost&         host_;
    uint32_t              pending_reg_[kNumRegs] = {};
    bool                  pending_active_ = false;
    uint32_t              expected_dwords_ = 0;
};
