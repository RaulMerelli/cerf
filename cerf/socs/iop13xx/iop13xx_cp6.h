#pragma once

#include "../irq_controller.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

struct BlockContext;
struct DecodedInsn;

/* IOP13xx CP6 system-control block: 128-source interrupt controller and the
   two decrementing timers used by the Siemens P377 OAL. */
class Iop13xxCp6 : public IrqController {
public:
    using IrqController::IrqController;
    ~Iop13xxCp6() override;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void AssertIrq   (int source_bit) override;
    void AssertSubIrq(int main_source_bit, int sub_source_bit) override;
    void DeAssertIrq (int source_bit) override;
    void DeliverPendingIrq() override;

    uint8_t* EmitRegisterTransfer(uint8_t* cursor, DecodedInsn* d,
                                  BlockContext* ctx);

    static uint32_t __fastcall ReadHelper(Iop13xxCp6* self, uint32_t key);
    static void __fastcall WriteHelper(Iop13xxCp6* self, uint32_t key,
                                       uint32_t value);

private:
    struct Timer {
        uint32_t control = 0;
        uint32_t counter = 0;
        uint32_t reload = 0;
        uint32_t base_cycles = 0;
    };

    static constexpr uint32_t kTimerEnable = 0x02u;
    static constexpr uint32_t kTimerReload = 0x04u;
    static constexpr uint32_t kTimer0Irq = 8u;
    static constexpr uint32_t kTimerDivider = 32u;

    uint32_t GuestCycles() const;
    void TimerLoop();
    void AdvanceTimersLocked(uint32_t cycles_now);
    void AdvanceTimerLocked(Timer& timer, uint32_t cycles_now,
                            uint32_t status_bit);
    uint32_t ReadRegisterLocked(uint32_t key, uint32_t cycles_now);
    void WriteRegisterLocked(uint32_t key, uint32_t value,
                             uint32_t cycles_now);
    uint32_t InterruptVectorLocked() const;
    bool HasPendingIrqLocked() const;
    void NotifyLocked();

    mutable std::mutex state_mutex_;
    uint32_t intctl_[4]{};
    uint32_t intstr_[4]{};
    uint32_t pending_[4]{};
    uint32_t intbase_ = 0;
    uint32_t intsize_ = 0;
    uint32_t tisr_ = 0;
    uint32_t wdtcr_ = 0;
    uint32_t wdtsr_ = 0;
    Timer timer_[2]{};

    std::thread timer_thread_;
    std::atomic<bool> stop_thread_{false};
};
