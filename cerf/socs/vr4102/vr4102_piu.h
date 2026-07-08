#pragma once

#include "../../peripherals/peripheral_base.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

/* NEC VR4102 PIU (Touch Panel Interface Unit), UM ch.19. Two MMIO blocks: PIU1
   control 0x0B000120 (this Peripheral) + PIU2 data buffers 0x0B0002A0 (served by
   Vr4102Piu2Mmio). Resistive-panel A/D scan sequencer feeding touch.dll. */
class Vr4102Piu : public Peripheral {
public:
    using Peripheral::Peripheral;

    ~Vr4102Piu() override { StopWorker(); }
    void OnShutdown() override { StopWorker(); }

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x0B000120u; }  /* PIU1 control block */
    uint32_t MmioSize() const override { return 0x20u; }        /* 0x120-0x13F */

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("PIU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("PIU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PIU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("PIU WriteWord", addr, v); }

    /* PIU2 data-buffer block (0x0B0002A0) accessors, driven by Vr4102Piu2Mmio. */
    uint16_t ReadHalf2(uint32_t off);
    void     WriteHalf2(uint32_t off, uint16_t value);

    /* Host stylus source: pen contact + normalized position, both axes in the
       PIU's 10-bit A/D range [0,1023] (the board TouchInput adapter maps host
       surface coords to this). */
    void SetPen(bool down, uint16_t pos_x, uint16_t pos_y);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    /* PADSTATE (PIUCNTREG D12:10) scan-sequencer states (UM 19.3.1, p387). */
    enum : uint16_t {
        kStDisable      = 0,
        kStStandby      = 1,
        kStAdPortScan   = 2,
        kStWaitPenTouch = 4,
        kStPenDataScan  = 5,
        kStIntervalNext = 6,
        kStCmdScan      = 7,
    };

    /* PIUMODE (PIUCNTREG D4:3, UM 19.3.1 p388): 00 = sample coordinate data,
       01 = operate A/D converter using any command. */
    uint16_t PiuMode() const { return (cnt_cfg_ >> 3) & 0x3u; }

    void     ApplyCntWriteLocked(uint16_t value);
    void     SampleOnceLocked();       /* fill the next page buffer + raise interrupt */
    void     CmdScanOnceLocked();      /* A/D command scan: convert PIUCMDREG.ADCMD -> PIUAB0 */
    void     DriveIcuLocked();         /* push PIUINTREG causes to the ICU PIUINT source */
    uint32_t IntervalMsLocked() const; /* SCANINTVAL x 30us -> worker cadence, clamped */
    void     StopWorker();
    void     WorkerLoop();

    mutable std::mutex mtx_;

    /* Control/config registers (UM 19.3.x). state_ is the PADSTATE field. */
    uint16_t state_       = kStDisable;
    uint16_t cnt_cfg_     = 0;        /* PIUCNTREG R/W config bits (mode/scan/power) */
    uint16_t intreg_      = 0;        /* PIUINTREG causes {D0,D2-D6} + OVP (D15) */
    uint16_t sivl_        = 0x0007u;  /* PIUSIVLREG D10:0 (reset 0x007) */
    uint16_t stbl_        = 0x0007u;  /* PIUSTBLREG D5:0 (reset 0x007) */
    uint16_t cmd_         = 0x000Fu;  /* PIUCMDREG D12:0 (reset 0x00F) */

    /* Pen contact: current + previous (PIUCNTREG D13 PENSTC / D14 PENSTP). */
    bool     pen_cur_     = false;
    bool     pen_prev_    = false;
    uint16_t pos_x_       = 0;        /* latest host position, [0,1023] */
    uint16_t pos_y_       = 0;

    /* Two ping-pong coordinate page buffers, 5 halfwords each: X+,X-,Y+,Y-,Z.
       D15 VALID + D9:0 PADDATA (UM 19.3.9 p399). */
    uint16_t page_buf_[2][5] = {};
    uint16_t next_page_   = 0;        /* page written by the next sample */

    /* PIUAB0REG (0x0B0002B0): the command-scan A/D result buffer (UM 19.3.10
       p400, Table 19-5 "During CMDScan" -> PIUAB0REG). D15 VALID + D9:0 PADDATA. */
    uint16_t adbuf_       = 0;

    std::mutex              cv_mtx_;
    std::condition_variable cv_;
    std::thread             worker_;
    std::atomic<bool>       stop_{false};
    std::atomic<bool>       wake_{false};   /* pen-edge wake, lost-wakeup safe */
};
