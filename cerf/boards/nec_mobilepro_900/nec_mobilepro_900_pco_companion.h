#pragma once

#include "../../core/service.h"

#include <atomic>
#include <cstdint>
#include <mutex>

/* NEC P530 "PCO" companion MCU (keyboard + touch). It has NO MMIO of its own:
   the in-ROM pco.dll talks to it over the PXA255 BTUART, so CERF drives input
   purely by feeding report bytes into the BTUART RX FIFO (Uart16550::PushRx).
   Report byte protocol RE'd from pco.dll sub_1BC28B4. */
class NecMobilePro900PcoCompanion : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;

    /* Main-battery raw value the PCO returns for an IOCTL-0xE main read. The CE
       battery driver (battery.dll sub_1BC2368) writes 0x70 to BTUART THR and waits
       for a [0x70][hi][lo] reply over RX; battery.dll maps the 16-bit value through
       a voltage table. The board battery service computes this from the widget %. */
    void SetMainBatteryRaw(uint16_t raw) { main_battery_raw_.store(raw, std::memory_order_release); }

    /* Stream a 13-byte (104-bit) key-matrix snapshot: opcode 0x13, the 13 matrix
       bytes (pco accumulates them via sub_1BC1C54), then 0x12 (pco signals
       event.pco.keyboard). Public so the dev bit-sweep probe can map matrix-bit
       -> VK against keybddr.dll's matrix decode. */
    void SendKeyboardMatrix(const uint8_t matrix[13]);

    /* Touch report: opcode 0x04 then 16-bit X, Y big-endian (pco sub_1BC28B4
       states 4-7); pco signals event.pco.touch and touch.dll calibrates. */
    void SendTouch(uint16_t x, uint16_t y);
    /* Pen-up: opcode 0x05 (pco clears its touch state, signals event.pco.touch). */
    void SendPenUp();

private:
    void PushByte(uint8_t b);
    /* BTUART TX observer: the guest writes a single PIC command byte to THR.
       0x70 = main-battery-state request -> answer with a [0x70][hi][lo] packet. */
    void OnBtuartTx(uint8_t b);

    std::mutex report_mtx_;   /* serializes a full report's bytes into the RX FIFO */
    std::atomic<uint16_t> main_battery_raw_{0};
};
