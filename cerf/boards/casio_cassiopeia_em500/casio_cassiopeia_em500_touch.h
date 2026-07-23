#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class CerfEmulator;
class StateWriter;
class StateReader;

/* touch.dll (CE touch-panel PDD, imgbase 0xF90000): sub_F91DDC @0xF91DDC,
   loc_F91958, loc_F91D40, sub_F92DDC @0xF92DDC; register window in the companion
   at base 0x0A000000. */
class CasioCassiopeiaEm500Touch {
public:
    ~CasioCassiopeiaEm500Touch();

    void Init(CerfEmulator& emu);
    void OnShutdown();

    bool TryReadByte (uint32_t off, uint8_t&  out);
    bool TryWriteByte(uint32_t off, uint8_t   value);
    bool TryReadHalf (uint32_t off, uint16_t& out);
    bool TryWriteHalf(uint32_t off, uint16_t  value);
    bool TryReadWord (uint32_t off, uint32_t& out);
    bool TryWriteWord(uint32_t off, uint32_t  value);

    void SetPen(bool down, int surface_x, int surface_y);
    void OnCaptureLost();

    /* touch.dll loc_F91958 @0xF91A82 (0x304 & 0x18 = A/D-done cause, SYSINTR 17). */
    bool SamplePending() const { return sample_pending_.load(std::memory_order_acquire); }
    void AckSample()           { sample_pending_.store(false, std::memory_order_release); }
    /* touch.dll loc_F91958 @0xF91A38 (0x304 & 1 = pen-event cause, SYSINTR 24);
       drives the cause&1 ACK path @0xF91A3C that resets the median index
       dword_F9410C @0xF91A4E when dword_F94118==0 (post-release). */
    bool ReleaseAckPending() const { return release_ack_.load(std::memory_order_acquire); }

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    void StopWorker();
    void WorkerLoop();
    void PresentDownLocked();
    void PresentLiftLocked();
    void DepositGate();

    CerfEmulator* emu_ = nullptr;

    mutable std::mutex mtx_;

    /* touch.dll sub_F91DDC @0xF91E16/@0xF91E22, loc_F91958 @0xF919BA,
       loc_F91B62 @0xF91B70; nk_main_kernel.exe sub_9F032B60 @0x9F032D30. */
    uint32_t ctrl_300_ = 0;
    /* touch.dll sub_F91DDC @0xF91E2C/@0xF91E34/@0xF91E3C/@0xF91E42. */
    uint32_t param_308_ = 0;
    uint32_t param_30C_ = 0;
    uint32_t param_310_ = 0;
    uint32_t param_318_ = 0;
    /* touch.dll sub_F91DDC @0xF91E02; nk_main_kernel.exe sub_9F032B60 @0x9F032D30. */
    uint32_t cfg_3C8_ = 0;
    /* touch.dll loc_F91D40 @0xF91D4E (mode0 0x320-0x32C) / @0xF91D7E (mode1
       0x350-0x35C); X=(hi[0]-hi[1]+0xFFF)>>1, Y=(hi[2]-hi[3]+0xFFF)>>1, &0xFFF. */
    uint16_t adc0_[4] = {};
    uint16_t adc1_[4] = {};

    uint16_t raw_x_ = 0;
    uint16_t raw_y_ = 0;
    bool     pen_down_ = false;

    std::atomic<bool> sample_pending_{false};
    std::atomic<bool> release_ack_{false};
    int release_drain_ = 0;
    bool pending_down_ = false;
    uint16_t pending_x_ = 0;
    uint16_t pending_y_ = 0;

    std::mutex              cv_mtx_;
    std::condition_variable cv_;
    std::thread             worker_;
    std::atomic<bool>       stop_{false};
    std::atomic<bool>       wake_{false};
};
