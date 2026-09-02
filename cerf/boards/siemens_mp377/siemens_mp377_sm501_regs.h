#pragma once

#include "siemens_mp377_sm501.h"

#include "../../peripherals/peripheral_base.h"
#include "../../state/state_stream.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace siemens_mp377 {

class SiemensMp377Sm501Ac97;
class SiemensMp377Sm501AudioMcu;
class SiemensMp377Sm501AudioOutput;
class SiemensMp377Sm501Dma;
class SiemensMp377Sm501PowerGpio;

class SiemensMp377Sm501Regs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint32_t PanelFbOffset() const;
    uint32_t PanelPitchBytes() const;
    uint32_t PanelWidthPixels() const;
    uint32_t PanelHeightLines() const;
    uint32_t ReadSm501Register(uint32_t offset) const;

    uint8_t ReadByte(uint32_t address) override;
    uint16_t ReadHalf(uint32_t address) override;
    uint32_t ReadWord(uint32_t address) override;
    void WriteByte(uint32_t address, uint8_t value) override;
    void WriteHalf(uint32_t address, uint16_t value) override;
    void WriteWord(uint32_t address, uint32_t value) override;

    void SaveState(StateWriter& writer) override;
    void RestoreState(StateReader& reader) override;

private:
    friend class SiemensMp377Sm501Dma;
    friend class SiemensMp377Sm501Ac97;
    friend class SiemensMp377Sm501AudioOutput;
    friend class SiemensMp377Sm501AudioMcu;
    friend class SiemensMp377Sm501PowerGpio;

    template <typename T> void WriteVectorState(StateWriter& writer, const std::vector<T>& values) const {
        const uint64_t size = static_cast<uint64_t>(values.size());
        writer.Write(size);
        if (size) writer.WriteBytes(values.data(), static_cast<size_t>(size * sizeof(T)));
    }

    template <typename T>
    void ReadVectorState(StateReader& reader, std::vector<T>& values, size_t max_expected, const char* what) {
        uint64_t size = 0;
        reader.Read(size);
        if (size > static_cast<uint64_t>(max_expected)) HaltUnsupportedAccess(what, kSm501RegsBarPa, size);
        values.resize(static_cast<size_t>(size));
        if (size) reader.ReadBytes(values.data(), static_cast<size_t>(size * sizeof(T)));
    }

    static uint32_t NormalizePanelFbOffset(uint32_t value);
    static uint32_t DecodePanelPitchBytes(uint32_t value);
    static uint32_t Lo16(uint32_t value);
    static uint32_t Hi16(uint32_t value);

    static constexpr uint32_t kSm501SupportedIrqBits = 0xFFFFFFFFu;
    uint32_t Sm501LatchedInterruptStatus() const;
    void RefreshSm501InterruptLine();
    void RaiseSm501InterruptBits(uint32_t bits);
    void ClearSm501InterruptBits(uint32_t bits);

    std::vector<uint32_t> regs_;
    uint32_t panel_fb_raw_ = 0u;
    uint32_t panel_pitch_bytes_ = 0u;
    bool sm501_irq_line_asserted_ = false;
};

} // namespace siemens_mp377
