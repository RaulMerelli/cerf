#pragma once

#include "../../core/service.h"

#include <array>
#include <cstdint>
#include <unordered_map>

class StateReader;
class StateWriter;

class SiemensMp377Ertec400Model final : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void BeginAccess();
    bool GetWord(uint32_t offset, uint32_t& value) const;
    void SetWord(uint32_t offset, uint32_t value);

    bool ConsumePrimaryReadback(uint32_t& value);
    bool ConsumeSecondaryReadback(uint32_t& value);
    uint32_t FreeRunningCounter10ns() const;
    uint32_t DefaultRead(uint32_t offset) const;

    void PublishIdleInitState();
    void PublishSwitchReady(uint32_t status);
    void ApplySwitchControl(uint32_t value);
    void CompletePrimaryCommand(uint32_t value);
    void CompleteSecondaryCommand(uint32_t value);
    void WriteMdioControl(uint32_t value);

    void SaveState(StateWriter& writer) const;
    void RestoreState(StateReader& reader);

private:
    static uint32_t CanonicalKey(uint32_t offset);
    static uint32_t MdioDefault(uint32_t reg);
    static bool IsNrtDmacCommand(uint32_t offset);

    bool ExposeImmediateReadback(uint64_t write_sequence) const;
    uint32_t ReadMdio(uint32_t reg) const;
    void WriteMdio(uint32_t reg, uint32_t value);

    std::unordered_map<uint32_t, uint32_t> words_;
    uint32_t mdio_control_ = 0;
    uint32_t last_mdio_register_ = 0x02u;
    std::array<uint32_t, 32> mdio_phy_regs_{};
    std::array<uint8_t, 32> mdio_phy_written_{};
    uint32_t switch_control_ = 0;
    uint32_t switch_status_ = 0x0000FFFFu;
    uint64_t access_sequence_ = 0;
    uint32_t primary_readback_ = 0;
    uint32_t secondary_readback_ = 0;
    uint64_t primary_readback_sequence_ = 0;
    uint64_t secondary_readback_sequence_ = 0;
    bool primary_readback_pending_ = false;
    bool secondary_readback_pending_ = false;
};

