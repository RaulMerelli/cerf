#pragma once

#include "../../core/service.h"

#include <atomic>
#include <cstdint>

class StateReader;
class StateWriter;

namespace siemens_mp377 {

class SiemensMp377TouchPanel : public Service {
public:
    using Service::Service;

    static constexpr int kTouchIrqSource = 0x23;

    bool ShouldRegister() override;
    void OnReady() override;

    void QueueSmiCommand(uint16_t cmd);
    uint32_t ReadSmiSampleWord();
    uint32_t ReadPenDetectReg();
    void UpdateHostPointer(int x, int y, bool down);
    void CaptureLost();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    bool EffectiveTouchDown() const;
    uint16_t PopSmiCommand();
    uint16_t NextAdcTransportHalfword();
    void HostPointToTouchRaw(uint32_t x, uint32_t y, uint16_t* raw_x, uint16_t* raw_y) const;

    std::atomic<uint32_t> smi_last_cmd_{0xE0u};
    std::atomic<uint32_t> smi_read_phase_{0};
    std::atomic<uint32_t> touch_down_{0};
    std::atomic<uint32_t> touch_x_{0};
    std::atomic<uint32_t> touch_y_{0};
    double touch_affine_[6]{};

    uint16_t smi_cmd_q_[16] = {};
    uint32_t smi_cmd_head_ = 0;
    uint32_t smi_cmd_tail_ = 0;
};

} // namespace siemens_mp377
