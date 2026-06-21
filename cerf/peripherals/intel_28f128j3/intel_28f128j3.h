#pragma once

#include "../peripheral_base.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"

#include <cstdint>
#include <utility>
#include <vector>

/* SIMpad SL4 NOR (SA-1110 CS0/CS1), 28F128J3 StrataFlash. Reads of a backed PA
   bypass this peripheral to the EmulatedMemory backing, so id / status / CFI and
   the per-block read-ID lock configuration are presented by MUTATING that
   backing and restored on read-array - a Read override here would never fire. */
class Intel28F128J3 : public Peripheral {
public:
    using Peripheral::Peripheral;

    void OnReady() override;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SimpadSl4;
    }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    /* Per-bank interleave geometry. A "lane" is one chip on the shared bus. */
    virtual uint32_t Parallel()    const = 0;  /* chips wired in parallel */
    virtual uint32_t DeviceWidth() const = 0;  /* bytes per chip (x8=1, x16=2) */

private:
    static constexpr uint16_t kMfr            = 0x0089u;
    static constexpr uint16_t kDevice         = 0x0018u;
    static constexpr uint32_t kChipEraseBlock = 0x20000u;  /* 128 KB/chip (datasheet CFI 0x2D-0x30) */
    static constexpr uint32_t kCfiFirst       = 0x10u;     /* first CFI word offset */
    static constexpr uint32_t kCfiCount       = 0x2Fu;     /* CFI words 0x10..0x3E */
    /* Byte offset of each block's lock bit in read-ID mode, read by the FLite
       probe at block_base + 8 (psmfsd sub_12AD330, 0x12AD854 `ADD R0,R6,#8`);
       bit0 = locked (datasheet Table 5). */
    static constexpr uint32_t kLockReadOffset = 8u;
    /* Status register bits, datasheet Table 6 (p20): SR7 ready, SR5 erase/
       clear-lock error, SR4 program/set-lock error, SR1 lock-detect abort. */
    static constexpr uint8_t  kSrReady     = 0x80u;
    static constexpr uint8_t  kSrEraseErr  = 0x20u;
    static constexpr uint8_t  kSrProgErr   = 0x10u;
    static constexpr uint8_t  kSrLockAbort = 0x02u;
    static const uint8_t      kCfi[];                      /* per-chip CFI bytes; size asserted == kCfiCount */

    enum class Mode : uint8_t { kArray, kProgramSetup, kEraseSetup, kLockSetup };

    uint32_t BusBytes()        const { return Parallel() * DeviceWidth(); }
    uint32_t EraseBlockBytes() const { return kChipEraseBlock * Parallel(); }
    uint32_t NumBlocks()       const { return MmioSize() / EraseBlockBytes(); }
    uint32_t BlockBase(uint32_t i)     const { return MmioBase() + i * EraseBlockBytes(); }
    uint32_t BlockIndex(uint32_t addr) const {
        return (addr - MmioBase()) / EraseBlockBytes();
    }
    uint32_t BlockIndexChecked(uint32_t addr);

    void     Command(uint32_t addr, uint32_t value, uint32_t width);
    void     RestoreArray();
    void     SaveAndWrite(uint32_t addr, uint8_t v);
    void     EmitLaneByte(uint32_t addr, uint8_t v);
    void     PresentStatus(uint32_t addr, uint8_t sr = kSrReady);
    void     PresentId();
    void     PresentCfi();
    void     CommitProgram(uint32_t addr, uint32_t value, uint32_t width);
    void     EraseBlock(uint32_t addr);
    uint8_t* Host(uint32_t addr);

    Mode mode_ = Mode::kArray;
    /* Transient ID/CFI/status presentations mutate the backing and are undone on
       read-array; saved as scattered (addr, original byte) pairs because the
       per-block lock presentation touches one location per erase block. */
    std::vector<std::pair<uint32_t, uint8_t>> shadow_;
    /* Per erase-block lock state (read-ID lock bit). Power-up = all unlocked. */
    std::vector<uint8_t> block_locked_;
};
