#pragma once

#include "../../socs/irq_controller.h"

#include <cstdint>
#include <mutex>

/* ODOREGS.H:8-14 source bit positions. */
constexpr int kSourceSystemIntr        = 0;   /* ODOREGS.H: odo_systemIntr        = 0x0001 */
constexpr int kSourceLcdIntr           = 2;   /* ODOREGS.H: odo_lcdIntr           = 0x0004 */
constexpr int kSourceProdSerialIntr    = 3;   /* ODOREGS.H: odo_prodSerialIntr    = 0x0008 */
constexpr int kSourceTouchAudioAdcIntr = 5;   /* ODOREGS.H: odo_touchAudioAdcIntr = 0x0020 */
constexpr int kSourceKeybIntr          = 6;   /* ODOREGS.H: odo_keybIntr          = 0x0040 */
constexpr int kSourceIrIntr            = 7;   /* ODOREGS.H: odo_irIntr            = 0x0080 */
constexpr int kSourceEtherIntr         = 8;   /* ODOREGS.H: odo_etherIntr         = 0x0100 */

class OdoArm720BoardIntc : public IrqController {
public:
    using IrqController::IrqController;

    bool ShouldRegister() override;

    void AssertIrq   (int source_bit)                          override;
    void AssertSubIrq(int main_source_bit, int sub_source_bit) override;
    void DeAssertIrq (int source_bit)                          override;
    void DeliverPendingIrq()                                   override;

    uint32_t ReadReg32 (uint32_t offset);
    uint16_t ReadReg16 (uint32_t offset);
    void     WriteReg32(uint32_t offset, uint32_t value);
    void     WriteReg16(uint32_t offset, uint16_t value);

private:
    bool HasPendingUnmaskedLocked() const;
    void NotifyJitInterruptState();

    std::mutex state_mutex_;
    uint32_t   cpu_isr_ = 0;
    uint32_t   cpu_mr_  = 0;
};
