#include "imx6_gic.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../jit/arm/arm_jit.h"
#include "../../state/emulation_freeze.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace {

class Imx6GicTick final : public Service {
public:
    using Service::Service;

    ~Imx6GicTick() override { OnShutdown(); }

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetSoc() == SocFamily::iMX6;
    }

    void OnReady() override {
        worker_ = std::thread(&Imx6GicTick::Run, this);
    }

    void OnShutdown() override {
        stop_.store(true, std::memory_order_release);
        if (worker_.joinable()) worker_.join();
    }

private:
    void Run() {
        auto& freeze = emu_.Get<EmulationFreeze>();
        auto& jit = emu_.Get<ArmJit>();
        auto& gic = emu_.Get<Imx6Gic>();
        while (!stop_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::microseconds(250));
            auto frozen = freeze.WorkerSection();
            if (gic.Tick()) jit.SetInterruptPending();
        }
    }

    std::thread worker_;
    std::atomic<bool> stop_{false};
};

}

REGISTER_SERVICE(Imx6GicTick);
