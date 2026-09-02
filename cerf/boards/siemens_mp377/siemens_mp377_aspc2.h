#pragma once

#include "../../peripherals/peripheral_base.h"

#include <array>
#include <cstdint>

namespace siemens_mp377 {

inline constexpr uint32_t kMp377Aspc2Base = 0xD0120000u;
inline constexpr uint32_t kMp377Aspc2Size = 0x00001000u;
inline constexpr uint32_t kMp377Aspc2RamVa = 0x9C140000u;
inline constexpr uint32_t kMp377Aspc2RamPa = 0xD0140000u;
inline constexpr uint32_t kMp377Aspc2RamSize = 0x00020000u;

class SiemensMp377Aspc2 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;
    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint8_t ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    uint32_t ReadWord(uint32_t addr) override;
    void WriteByte(uint32_t addr, uint8_t value) override;
    void WriteHalf(uint32_t addr, uint16_t value) override;
    void WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    static constexpr uint32_t kVersionOffset = 0x0Bu;
    static constexpr uint8_t kE2PlusVersion = 4u;

    std::array<uint8_t, kMp377Aspc2Size> registers_{};
};

} // namespace siemens_mp377
