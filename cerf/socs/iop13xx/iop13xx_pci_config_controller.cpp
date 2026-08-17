#include "iop13xx_pci_config.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

constexpr uint32_t kSecondaryStatusBase = 0xFFDC8000u;
constexpr uint32_t kSecondaryStatusSize = 0x00000010u;
constexpr uint32_t kSecondaryConfigBase = 0xFFDC832Cu;
constexpr uint32_t kPrimaryConfigBase = 0xFFDCD330u;
constexpr uint32_t kConfigSize = 0x00000008u;

bool IsIop13xx(CerfEmulator& emulator) {
    auto* board = emulator.TryGet<BoardContext>();
    return board && board->GetSoc() == SocFamily::IOP13xx;
}

class Iop13xxSecondaryAtuStatus final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return IsIop13xx(emu_); }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kSecondaryStatusBase; }
    uint32_t MmioSize() const override { return kSecondaryStatusSize; }

    uint32_t ReadWord(uint32_t) override { return 0; }
    uint16_t ReadHalf(uint32_t) override { return 0; }
    uint8_t ReadByte(uint32_t) override { return 0; }
    void WriteWord(uint32_t, uint32_t) override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteByte(uint32_t, uint8_t) override {}
};

class Iop13xxSecondaryPciConfig final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return IsIop13xx(emu_); }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kSecondaryConfigBase; }
    uint32_t MmioSize() const override { return kConfigSize; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - kSecondaryConfigBase) {
        case 0x00u:
            return occar_;
        case 0x04u:
            if (auto* config = emu_.TryGet<Iop13xxPciConfig>())
                return config->ReadSecondary(occar_);
            return 0xFFFFFFFFu;
        default:
            HaltUnsupportedAccess("IOP13xx secondary PCI config read", addr, 0);
        }
    }

    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t word = ReadWord(addr & ~3u);
        return static_cast<uint16_t>(word >> ((addr & 2u) * 8u));
    }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t word = ReadWord(addr & ~3u);
        return static_cast<uint8_t>(word >> ((addr & 3u) * 8u));
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - kSecondaryConfigBase) {
        case 0x00u:
            occar_ = value;
            return;
        case 0x04u:
            if (auto* config = emu_.TryGet<Iop13xxPciConfig>())
                config->WriteSecondary(occar_, value);
            return;
        default:
            HaltUnsupportedAccess("IOP13xx secondary PCI config write", addr, value);
        }
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 2u) * 8u;
        const uint32_t mask = 0xFFFFu << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) |
                           (static_cast<uint32_t>(value) << shift));
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = 0xFFu << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) |
                           (static_cast<uint32_t>(value) << shift));
    }

    void SaveState(StateWriter& writer) override { writer.Write(occar_); }
    void RestoreState(StateReader& reader) override { reader.Read(occar_); }

private:
    uint32_t occar_ = 0;
};

class Iop13xxPrimaryPciConfig final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return IsIop13xx(emu_); }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kPrimaryConfigBase; }
    uint32_t MmioSize() const override { return kConfigSize; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - kPrimaryConfigBase) {
        case 0x00u:
            return occar_;
        case 0x04u:
            if (auto* config = emu_.TryGet<Iop13xxPciConfig>())
                return config->ReadPrimary(occar_);
            return 0xFFFFFFFFu;
        default:
            HaltUnsupportedAccess("IOP13xx primary PCI config read", addr, 0);
        }
    }

    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t word = ReadWord(addr & ~3u);
        return static_cast<uint16_t>(word >> ((addr & 2u) * 8u));
    }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t word = ReadWord(addr & ~3u);
        return static_cast<uint8_t>(word >> ((addr & 3u) * 8u));
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - kPrimaryConfigBase) {
        case 0x00u:
            occar_ = value;
            return;
        case 0x04u:
            if (auto* config = emu_.TryGet<Iop13xxPciConfig>()) {
                if (!config->WritePrimary(occar_, value))
                    HaltUnsupportedAccess("SM501 PCI config register write", addr, value);
            }
            return;
        default:
            HaltUnsupportedAccess("IOP13xx primary PCI config write", addr, value);
        }
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        HaltUnsupportedAccess("IOP13xx primary PCI config halfword write", addr, value);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("IOP13xx primary PCI config byte write", addr, value);
    }

    void SaveState(StateWriter& writer) override {
        writer.Write(occar_);
        if (auto* config = emu_.TryGet<Iop13xxPciConfig>())
            config->SaveState(writer);
    }

    void RestoreState(StateReader& reader) override {
        reader.Read(occar_);
        if (auto* config = emu_.TryGet<Iop13xxPciConfig>())
            config->RestoreState(reader);
    }

private:
    uint32_t occar_ = 0;
};

REGISTER_SERVICE(Iop13xxSecondaryAtuStatus);
REGISTER_SERVICE(Iop13xxSecondaryPciConfig);
REGISTER_SERVICE(Iop13xxPrimaryPciConfig);

}

