#include "../../socs/iop13xx/iop13xx_pci_config.h"

#include "siemens_mp377_ertec400.h"
#include "siemens_mp377_sm501.h"
#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"

#include <array>
#include <cstdint>

namespace {

class SiemensMp377PciConfig final : public Iop13xxPciConfig {
public:
    using Iop13xxPciConfig::Iop13xxPciConfig;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoard() == Board::SiemensMP377;
    }

    void OnReady() override {
        BuildSm501Config();
        BuildErtec400Config();
    }

    uint32_t ReadPrimary(uint32_t occar) override {
        const uint32_t reg = (occar & 0xFFu) >> 2;
        const uint32_t select = DeviceSelect(occar);
        if (select == kSm501Select) return reg < sm501_cfg_.size() ? Sm501ConfigRead(reg) : 0xFFFFFFFFu;
        if (select == kErtec400Select) return reg < ertec_cfg_.size() ? ertec_cfg_[reg] : 0xFFFFFFFFu;
        return 0xFFFFFFFFu;
    }

    bool WritePrimary(uint32_t occar, uint32_t value) override {
        const uint32_t reg = (occar & 0xFFu) >> 2;
        const uint32_t select = DeviceSelect(occar);
        if (select == kErtec400Select) {
            WriteErtec400Config(reg, value);
            return true;
        }
        if (select != kSm501Select || reg >= sm501_cfg_.size()) return true;
        return WriteSm501Config(reg, value);
    }

    uint32_t ReadSecondary(uint32_t occar) override {
        const uint32_t reg = (occar & 0xFFu) >> 2;
        if (DeviceSelect(occar) != kSecondarySm501Select || reg >= sm501_cfg_.size()) {
            return 0xFFFFFFFFu;
        }
        return Sm501ConfigRead(reg);
    }
    bool WriteSecondary(uint32_t occar, uint32_t value) override {
        const uint32_t reg = (occar & 0xFFu) >> 2;
        if (DeviceSelect(occar) == kSecondarySm501Select && reg < sm501_cfg_.size()) {
            WriteSm501Config(reg, value);
        }
        return true;
    }

    void SaveState(StateWriter& writer) override {
        writer.WriteBytes(sm501_cfg_.data(), sm501_cfg_.size() * sizeof(sm501_cfg_[0]));
        writer.WriteBytes(ertec_cfg_.data(), ertec_cfg_.size() * sizeof(ertec_cfg_[0]));
    }

    void RestoreState(StateReader& reader) override {
        reader.ReadBytes(sm501_cfg_.data(), sm501_cfg_.size() * sizeof(sm501_cfg_[0]));
        reader.ReadBytes(ertec_cfg_.data(), ertec_cfg_.size() * sizeof(ertec_cfg_[0]));
    }

private:
    static uint32_t DeviceSelect(uint32_t occar) { return occar & 0x7FFFFF00u; }

    /* Real PCI BAR semantics: a write of all-ones is the size probe and the
       next read must return the size mask, not the programmed base.  The
       guest PCI bus driver measures the BAR this way; forcing the base on
       probe reads broke its allocation math and shifted every driver base
       by +0x100000 under the correct JIT. */
    uint32_t Sm501ConfigRead(uint32_t reg) {
        if (reg == 0x04 && fb_bar_probe_)
            return siemens_mp377::kSm501PciFbBarSizeMask | siemens_mp377::kSm501PciFbBarFlags;
        if (reg == 0x05 && regs_bar_probe_)
            return siemens_mp377::kSm501PciRegsBarSizeMask | siemens_mp377::kSm501PciRegsBarFlags;
        return sm501_cfg_[reg];
    }

    bool fb_bar_probe_ = false;
    bool regs_bar_probe_ = false;

    void BuildSm501Config() {
        sm501_cfg_.fill(0u);
        sm501_cfg_[0x00] = siemens_mp377::kSm501PciDeviceVendorDword;
        sm501_cfg_[0x01] = siemens_mp377::kSm501PciCommandStatusDword;
        sm501_cfg_[0x02] = siemens_mp377::kSm501PciClassDisplayDword;
        sm501_cfg_[0x03] = siemens_mp377::kSm501PciHeaderTypeDword;
        sm501_cfg_[0x04] = siemens_mp377::kSm501PciFbBarDword;
        sm501_cfg_[0x05] = siemens_mp377::kSm501PciRegsBarDword;
        sm501_cfg_[0x0B] = siemens_mp377::kSm501PciSubsystemSiemensDword;
        sm501_cfg_[0x0D] = siemens_mp377::kSm501PciCapabilityPointerDword;
        sm501_cfg_[0x0F] = siemens_mp377::kSm501PciInterruptPinIntaLine0Dword;
    }

    void BuildErtec400Config() {
        ertec_cfg_.fill(0u);
        /* siemens_mp377_v1040 eddertec400.dll sub_28E1CA8 PCI identity and BAR layout. */
        ertec_cfg_[0x00] = 0x4026110Au;
        ertec_cfg_[0x01] = 0x02000007u;
        ertec_cfg_[0x02] = 0x02000000u;
        ertec_cfg_[0x03] = 0x00000000u;
        ertec_cfg_[0x04] = siemens_mp377::kErtecBar0Base;
        ertec_cfg_[0x05] = siemens_mp377::kErtecBar1Base;
        ertec_cfg_[0x06] = siemens_mp377::kErtecBar2Base;
        ertec_cfg_[0x07] = siemens_mp377::kErtecBar3Base;
        ertec_cfg_[0x08] = siemens_mp377::kErtecBar4Base;
        ertec_cfg_[0x09] = siemens_mp377::kErtecBar5Base;
        ertec_cfg_[0x0B] = 0x4026110Au;
        ertec_cfg_[0x0F] = 0x00000100u;
    }

    void WriteErtec400Config(uint32_t reg, uint32_t value) {
        if (reg >= ertec_cfg_.size()) return;
        switch (reg) {
        case 0x00:
        case 0x02:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0E: return;
        case 0x01:
        case 0x03: ertec_cfg_[reg] = (ertec_cfg_[reg] & 0xFFFF0000u) | (value & 0x0000FFFFu); return;
        case 0x0C:
        case 0x0D: ertec_cfg_[reg] = 0u; return;
        case 0x0F: ertec_cfg_[reg] = (ertec_cfg_[reg] & 0xFFFFFF00u) | (value & 0x000000FFu); return;
        default: return;
        }
    }

    bool WriteSm501Config(uint32_t reg, uint32_t value) {
        switch (reg) {
        case 0x00:
        case 0x02:
        case 0x0A:
        case 0x0B:
        case 0x0D:
        case 0x0E: return true;
        case 0x01: sm501_cfg_[reg] = (sm501_cfg_[reg] & 0xFFFF0000u) | (value & 0x0000FFFFu); return true;
        case 0x03: sm501_cfg_[reg] = (sm501_cfg_[reg] & 0xFFFF0000u) | (value & 0x0000FFFFu); return true;
        case 0x04:
            fb_bar_probe_ = (value == 0xFFFFFFFFu);
            sm501_cfg_[reg] = siemens_mp377::kSm501PciFbBarDword;
            return true;
        case 0x05:
            regs_bar_probe_ = (value == 0xFFFFFFFFu);
            sm501_cfg_[reg] = siemens_mp377::kSm501PciRegsBarDword;
            return true;
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0C: sm501_cfg_[reg] = 0u; return true;
        case 0x0F: sm501_cfg_[reg] = (sm501_cfg_[reg] & 0xFFFFFF00u) | (value & 0x000000FFu); return true;
        default: return false;
        }
    }

    static constexpr uint32_t kSm501Select = 0x00007800u;
    static constexpr uint32_t kSecondarySm501Select = 0x00007800u;
    static constexpr uint32_t kErtec400Select = siemens_mp377::kErtec400PciSelect;

    std::array<uint32_t, 64> sm501_cfg_{};
    std::array<uint32_t, 64> ertec_cfg_{};
};

REGISTER_SERVICE_AS(SiemensMp377PciConfig, Iop13xxPciConfig);

} // namespace
