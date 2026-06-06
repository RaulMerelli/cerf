#pragma once

#include <cstdint>
#include <deque>

class Sed1356;

/* S1D13506 2D BitBLT engine (Technical Manual §8.3.12, Programming Notes
   §10). Synchronous: CPU-sourced ops buffer data-port words until the op's
   expected word count arrives. */
class Sed1356BitBlt {
public:
    explicit Sed1356BitBlt(Sed1356& owner) : owner_(owner) {}

    /* REG[100h] write with bit 7 set latches the op (Programming Notes
       §10.1: write data path initiates the 2D operation). */
    void Start();

    /* REG[100h] read-back: bit 7 busy, bit 6 FIFO not empty, bit 5 FIFO
       half full, bit 4 FIFO full, bits 1:0 dest/src linear (§10.1). */
    uint8_t Status() const;

    /* BitBLT data aperture (+0x100000, 16-bit). */
    void     DataWrite(uint16_t value);
    uint16_t DataRead();

private:
    enum Op : uint8_t {                      /* Table 8-32 / Table 10-2. */
        kWriteRop          = 0x0,
        kRead              = 0x1,
        kMovePosRop        = 0x2,
        kMoveNegRop        = 0x3,
        kTransparentWrite  = 0x4,
        kTransparentMovePos= 0x5,
        kPatternFillRop    = 0x6,
        kPatternFillTransp = 0x7,
        kColorExpand       = 0x8,
        kColorExpandTransp = 0x9,
        kMoveColorExpand   = 0xA,
        kMoveColorExpandTr = 0xB,
        kSolidFill         = 0xC,
    };

    struct Params {
        uint8_t  op = 0, rop = 0;
        bool     bpp16 = false;              /* REG[101h] bit 0.            */
        uint32_t src = 0, dst = 0;           /* 21-bit byte addresses.      */
        uint32_t width = 0, height = 0;      /* pixels/lines (already +1).  */
        uint32_t src_stride = 0, dst_stride = 0;  /* line-to-line bytes.    */
        uint32_t px = 1;                     /* bytes per pixel.            */
        uint16_t bg = 0, fg = 0;
    };

    void ExecuteDisplayOp();                 /* src+dst both in display memory. */
    void ExecuteCpuWrite();                  /* buffered CPU-sourced op.        */
    void RenderReadFifo();                   /* Read BitBLT -> out_fifo_.       */
    uint32_t ExpectedWriteWords() const;     /* Programming Notes §10.2 formulas. */
    void     ExpandLine(uint32_t line, const uint16_t* words, uint32_t nwords,
                        uint32_t skip, bool transparent);

    uint16_t ReadPxAt (uint32_t byte_addr) const;
    void     WritePxAt(uint32_t byte_addr, uint16_t v);
    uint16_t PatternPx(uint32_t x, uint32_t y) const;

    static uint16_t Rop(uint8_t code, uint16_t s, uint16_t d);

    Sed1356&  owner_;
    Params    p_{};
    bool      cpu_op_active_ = false;
    uint32_t  expected_words_ = 0;
    std::deque<uint16_t> in_fifo_;
    std::deque<uint16_t> out_fifo_;
};
