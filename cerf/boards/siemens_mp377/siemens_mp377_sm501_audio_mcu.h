#pragma once

#include "../../core/service.h"
#include "../../cpu/mcs51/mcs51_core.h"

#include <cstdint>

class StateReader;
class StateWriter;

namespace siemens_mp377 {

class SiemensMp377Sm501Regs;

// SM501 Databook ch. 12 controller: 8051 core, SRAM ownership and the
// CPU/8051 protocol mailbox used by the MP377 VGXaudio firmware.
class SiemensMp377Sm501AudioMcu : public Service, private Mcs51Bus {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    static bool IsControlRegister(uint32_t offset);
    static bool IsSram(uint32_t offset);
    static bool IsPcmBuffer(uint32_t offset);
    static constexpr uint32_t kOutputIrqBit = 1u << 10;
    static constexpr uint32_t kSramBase = 0x0C0000u;
    static constexpr uint32_t kSramLimit = 0x0C4000u;

    uint32_t ReadControl(uint32_t address, uint32_t offset);
    uint32_t ReadSramWord(uint32_t address, uint32_t offset);
    void WriteControl(uint32_t offset, uint32_t old_value, uint32_t value);
    void WriteSramWord(uint32_t address, uint32_t offset,
                       uint32_t old_value, uint32_t value);
    uint8_t ReadSramByte(uint32_t offset) const;
    void WriteSramByte(uint32_t offset, uint8_t value);
    void RunMmioSlice(uint32_t budget);
    void RaiseOutputIrq(uint8_t buffer_bit);
    void AcknowledgeInterrupts(uint32_t value);
    bool OutputTokenPending() const;

    void SaveState(StateWriter& writer) const;
    void RestoreState(StateReader& reader);

private:
    static constexpr uint32_t kControlBase = 0x0B0000u;
    static constexpr uint32_t kControlEnd = 0x0B0010u;
    static constexpr uint32_t kProgramBase = kSramBase;
    static constexpr uint32_t kProgramEnd = 0x0C3000u;
    static constexpr uint32_t kDualPortBase = 0x0C3000u;
    static constexpr uint32_t kSramEnd = kSramLimit;
    static constexpr uint32_t kResetReg = 0x0B0000u;
    static constexpr uint32_t kModeReg = 0x0B0004u;
    static constexpr uint32_t kToCpuIrqReg = 0x0B0008u;
    static constexpr uint32_t kFromCpuIrqReg = 0x0B000Cu;
    static constexpr uint32_t kMailboxCmd = 0x0C3FF0u;
    static constexpr uint32_t kMailboxStatus = 0x0C3FF1u;
    static constexpr uint32_t kMailboxArg0 = 0x0C3FF2u;
    static constexpr uint32_t kMailboxArg1 = 0x0C3FF6u;
    static constexpr uint32_t kMailboxIrqType = 0x0C3FFCu;
    static constexpr uint32_t kMailboxBusy = 0x0C3FFDu;
    static constexpr uint32_t kMailboxReady = 0x0C3FFFu;
    static constexpr uint32_t kOutputBufferA = 0x0C3000u;
    static constexpr uint32_t kOutputBufferB = 0x0C3600u;
    static constexpr uint32_t kOutputBufferBytes = 0x600u;
    static constexpr uint32_t kI2sIrqBit = 1u << 18;

    bool IsEnabled() const;
    static bool IsProgramSram(uint32_t offset);
    void Reset();
    void RunSlice(uint32_t budget);
    uint8_t AudioByte(uint32_t offset) const;
    void SetAudioByte(uint32_t offset, uint8_t value);
    uint32_t AudioDword(uint32_t offset) const;
    void SetAudioDword(uint32_t offset, uint32_t value);
    void CompleteMailboxCommand();
    void ServiceMailboxFallback();
    void HandleMailboxCommand(uint8_t cmd, uint32_t arg0, uint32_t arg1);
    void ClearProtocolInterrupt();

    uint8_t FetchCode(uint16_t address) const override;
    uint8_t ReadExternal(uint16_t address) override;
    void WriteExternal(uint16_t address, uint8_t value) override;

    Mcs51Core core_;
    bool enabled_ = false;
    bool from_cpu_irq_pending_ = false;
    uint32_t to_cpu_token_ = 0u;
    uint32_t from_cpu_token_ = 0u;
    uint8_t mailbox_busy_reads_ = 0u;
    uint64_t register_log_count_ = 0u;
};

} // namespace siemens_mp377

