#pragma once

#include <cstdint>
#include <vector>

class MediaQMq1188;

/* MediaQ MQ-1100/1132 2D Graphics Engine. The driver writes GE registers via
   the queued command aperture at register-window 0x1400 (aliases the direct GE
   block at 0x200) — routing it to RAM instead of here drops every accelerated
   blit. GE00R write triggers; CPU source data arrives via Source FIFO 0xC00. */
class MediaQMq1188Ge {
public:
    explicit MediaQMq1188Ge(MediaQMq1188& owner) : owner_(owner) {}

    /* g = GE register index (dword offset / 4 within the GE register block).
       g == 0 (GE00R Drawing Command) latches the command and executes it. */
    void     WriteReg(uint32_t g, uint32_t value);
    uint32_t ReadReg(uint32_t g) const { return g < kNumRegs ? reg_[g] : 0u; }

    /* Source FIFO port (register-window 0xC00). The latched blit executes here
       once its full source has arrived. */
    void PushSourceFifo(uint32_t value);

    /* Backstop: draw a latched blit on the GE-idle status read (CC01R) in the
       rare case its source under-delivers relative to the predicted count. */
    void FlushPending();

    /* CC01R Source/Command FIFO + GE status (register-window 0x004, Reg 4-10,
       datasheet p.72): CFS[4:0]=0x10 command FIFO empty, SFS[12:8]=0x10 source
       FIFO empty, bit16 GE-busy = 0. Synchronous execution => always ready. */
    static constexpr uint32_t StatusReady() { return 0x10u | (0x10u << 8); }

private:
    static constexpr uint32_t kNumRegs = 32u;

    /* GE register block dword indices (Table 4-11). The 0x1400 command
       aperture and the 0x200 direct block both map onto these. */
    enum : uint32_t {
        kGe00Command   = 0,   /* Drawing Command Register (Reg 4-83). */
        kGe01Size      = 1,   /* BitBLT width[11:0] | height[27:16] (Reg 4-84). */
        kGe02DstXY     = 2,   /* Destination X[11:0] | Y[27:16] (Reg 4-86). */
        kGe03SrcXY     = 3,   /* Source X[11:0] | Y[27:16] (Reg 4-88). */
        kGe04ColorCmp  = 4,   /* Colour compare / transparency key (Reg 4-90). */
        kGe05ClipLT    = 5,   /* Clip left[11:0] | top[27:16] (Reg 4-91). */
        kGe06ClipRB    = 6,   /* Clip right[11:0] | bottom[27:16] (Reg 4-92). */
        kGe07FgColor   = 7,   /* Foreground colour, mono source (Reg 4-93). */
        kGe08BgColor   = 8,   /* Background colour, mono source (Reg 4-94). */
        kGe09SrcStride = 9,   /* Source stride / pack-mode (Reg 4-95/4-96). */
        kGe0ADstStride = 10,  /* Dest stride[9:0] + depth[31:30] (Reg 4-97). */
        kGe0BBase      = 11,  /* Base address[19:0] of dest window (Reg 4-98). */
        kGe0CCmdStart  = 12,  /* Command start control (Reg 4-99). */
        kGe10Pat0      = 16,  /* Monochrome pattern data 0 (Reg 4-104). */
        kGe11Pat1      = 17,  /* Monochrome pattern data 1 (Reg 4-105). */
        kGe12PatFg     = 18,  /* Pattern foreground colour (Reg 4-106). */
        kGe13PatBg     = 19,  /* Pattern background colour (Reg 4-107). */
    };

    /* GE00R Drawing Command bit fields (Reg 4-83, datasheet pp.133-136). */
    static constexpr uint32_t kCmdRopMask    = 0x000000FFu; /* [7:0]  raster op. */
    static constexpr uint32_t kCmdTypeShift  = 8u;          /* [10:8] command type. */
    static constexpr uint32_t kCmdTypeMask   = 0x7u;
    static constexpr uint32_t kTypeBitBlt    = 0x2u;        /* 010 = BitBLT. */
    static constexpr uint32_t kTypeLine      = 0x4u;        /* 100 = Bresenham line. */
    static constexpr uint32_t kCmdXNeg       = 1u << 11;    /* X direction = negative. */
    static constexpr uint32_t kCmdYNeg       = 1u << 12;    /* Y direction = negative. */
    static constexpr uint32_t kCmdSrcSystem  = 1u << 13;    /* source in system memory (Source FIFO). */
    static constexpr uint32_t kCmdMonoSrc    = 1u << 14;    /* monochrome source. */
    static constexpr uint32_t kCmdMonoPat    = 1u << 15;    /* monochrome pattern. */
    static constexpr uint32_t kCmdColorTrans = 1u << 16;    /* colour transparency enable. */
    static constexpr uint32_t kCmdColorTrPol = 1u << 17;    /* colour transparency polarity. */
    static constexpr uint32_t kCmdMonoTrans  = 1u << 18;    /* mono transparency enable. */
    static constexpr uint32_t kCmdMonoTrPol  = 1u << 19;    /* mono transparency polarity. */
    static constexpr uint32_t kCmdPacked     = 1u << 20;    /* packed source mode (GE09R). */
    static constexpr uint32_t kCmdSolidSrc   = 1u << 23;    /* solid source = foreground colour. */
    static constexpr uint32_t kCmdRop2       = 1u << 25;    /* ROP2 code: [3:0] duplicated to [7:4] (Reg 4-83). */
    static constexpr uint32_t kCmdEnClip     = 1u << 26;    /* clipping enable. */
    static constexpr uint32_t kCmdSolidPat   = 1u << 30;    /* solid pattern = pattern fg colour. */

    void     Execute();
    void     ExecutePending();
    uint32_t ExpectedSourceDwords() const;
    void     FillSolid(uint8_t rop, uint32_t color, uint32_t cmd);
    void     DrawLine(uint32_t cmd);
    void     BlitColorSource(const uint32_t* r);
    void     BlitMonoSource(const uint32_t* r);
    void     BlitColorFromDisplay(uint32_t cmd);   /* screen-to-screen copy. */
    void     BlitMonoFromDisplay(uint32_t cmd);    /* mono source in display memory. */

    uint32_t DestStrideBytes() const { return reg_[kGe0ADstStride] & 0x3FFu; }    /* GE0AR[9:0]. */
    uint32_t BaseAddr()        const { return reg_[kGe0BBase] & 0xFFFFFu; }       /* GE0BR[19:0]. */
    uint32_t BytesPerPixel()   const;                                            /* GE0AR[31:30]. */

    static uint32_t Rop3(uint8_t rop, uint32_t p, uint32_t s, uint32_t d);

    MediaQMq1188&         owner_;
    uint32_t              reg_[kNumRegs] = {};
    std::vector<uint32_t> src_fifo_;
    uint32_t              pending_reg_[kNumRegs] = {};  /* GE state at a latched system-source command. */
    bool                  pending_active_ = false;
    uint32_t              expected_dwords_ = 0;  /* source dwords the latched blit will consume. */
};
