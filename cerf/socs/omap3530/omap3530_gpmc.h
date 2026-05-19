#pragma once

#include "omap3530_prcm_stub_block.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

class Omap3530Gpmc : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x6E000000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    void     OnReady()                                    override;
    uint16_t ReadHalf (uint32_t addr)                     override;
    uint8_t  ReadByte (uint32_t addr)                     override;
    void     WriteHalf(uint32_t addr, uint16_t value)     override;
    void     WriteWord(uint32_t addr, uint32_t value)     override;
    uint32_t ReadWord (uint32_t addr)                     override;

    /* Drain one 16-bit word from the prefetch FIFO for the configured
       CS. Called by Omap3530GpmcCs0Window peripheral when the driver
       memcpys from PA 0x08000000 — the FIFO data tap that fmd.c:393
       reads after STARTENGINE. */
    uint16_t DrainPrefetchByte16(uint32_t cs);

    /* Push one 16-bit word to the prefetch FIFO write-side. Called by
       Omap3530GpmcCs0Window on writes to PA 0x08000000 — the
       memcpy((BYTE*)pFifo, pData, ...) write side at fmd.c:456. */
    void PushPrefetchByte16(uint32_t cs, uint16_t value);

    /* Single-byte FIFO write — covers the byte-granular tail of an
       unaligned memcpy. Goes through the NAND state machine the
       same way as PushPrefetchByte16, but advances data_offset by 1
       instead of 2. */
    void PushPrefetchByte8 (uint32_t cs, uint8_t  value);

private:
    static constexpr int    kCsCount       = 8;
    static constexpr size_t kPageDataSize  = 2048;
    static constexpr size_t kPageSpareSize = 64;
    static constexpr size_t kPageTotalSize = kPageDataSize + kPageSpareSize;
    static constexpr size_t kPagesPerBlock = 64;
    static constexpr size_t kBlockCount    = 2048;
    static constexpr size_t kStorageSize   =
        kBlockCount * kPagesPerBlock * kPageTotalSize;

    enum class NandState {
        Idle,
        ReadId,
        StatusRead,
        ReadAddr,
        ReadDataReady,
        WriteAddr,
        WriteData,
        EraseAddr,
    };

    struct NandChip {
        NandState state          = NandState::Idle;
        int       id_byte_index  = 0;
        uint8_t   addr_bytes[5]  = {};
        int       addr_idx       = 0;
        size_t    data_offset    = 0;
        size_t    data_remaining = 0;
        std::vector<uint8_t> storage;
    };

    NandChip   nand_[kCsCount]{};
    std::mutex nand_mu_;

    static size_t PageByteOffset(uint8_t col_lo, uint8_t col_hi,
                                 uint8_t page0, uint8_t page1, uint8_t page2);

    void WriteCeBootMbr();

    void     WriteNandCommand(uint32_t cs, uint16_t cmd);
    void     WriteNandAddress(uint32_t cs, uint16_t addr);
    void     WriteNandData16 (uint32_t cs, uint16_t value);
    uint16_t ReadNandData16  (uint32_t cs);

    /* Do NOT wire AssertIrq(20) on IRQENABLE writes — the EVM3530 OAL
       installs no ISR for GPMC, so dispatch lands at an uninitialised
       entry and the kernel prefetch-aborts on OAL static-IO. */
    std::mutex irq_mu_;
    uint32_t   irq_status_ = 0;
    uint32_t   irq_enable_ = 0;

protected:
    const char* Label() const override { return "GPMC"; }
    const char* RegisterName(uint32_t off) const override;
};
