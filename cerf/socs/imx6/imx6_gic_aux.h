#pragma once

#include <cstdint>

class StateReader;
class StateWriter;

namespace imx6_gic_detail {

class Imx6GicAux {
public:
    bool ReadMmio(uint32_t off, uint32_t& value) const;
    bool WriteMmio(uint32_t off, uint32_t value);

    bool ScuRead(uint32_t off, uint32_t aux_control_register, uint32_t& value) const;
    bool ScuWrite(uint32_t off, uint32_t value, const char*& unmodelled);

    void AdvanceGlobalTimer(uint32_t cycles_now, uint32_t periph_div);
    bool GlobalTimerIrqPending() const;
    void ResetGlobalTimerAnchor(uint32_t cycles_now);

    void Reset();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    uint32_t scu_control_ = 0;
    uint32_t scu_access_control_ = 0x0000000Fu;
    uint32_t gt_anchor_cycles_ = 0;
    uint64_t gt_base64_ = 0;
    uint32_t global_timer_control_ = 0;
    uint32_t global_timer_status_ = 0;
    uint32_t global_timer_compare_lo_ = 0;
    uint32_t global_timer_compare_hi_ = 0;
    uint32_t global_timer_increment_ = 0;
};

}
