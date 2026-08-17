#pragma once

#include <cstddef>
#include <cstdint>

class EmulatedMemory;
class NetworkBackend;
class StateReader;
class StateWriter;

class Imx6FecLegacyRing {
public:
    void Reset();
    void SaveState(StateWriter& writer) const;
    void RestoreState(StateReader& reader);

    uint32_t Rdar() const { return rdar_; }
    uint32_t Tdar() const { return tdar_; }

    void SetRxDescriptorBase(uint32_t base);
    void SetTxDescriptorBase(uint32_t base);
    void Disable(uint32_t rx_base, uint32_t tx_base);
    void RequestReceive(EmulatedMemory& memory, bool controller_enabled);

    uint32_t RequestTransmit(EmulatedMemory& memory,
                             NetworkBackend& network,
                             bool controller_enabled,
                             bool cable_connected,
                             uint32_t tx_base);
    uint32_t Receive(EmulatedMemory& memory,
                     const uint8_t* frame,
                     std::size_t length,
                     uint32_t rx_base,
                     uint32_t max_receive_buffer);

private:
    void RefreshReceiveDemand(EmulatedMemory& memory);

    uint32_t rdar_ = 0u;
    uint32_t tdar_ = 0u;
    uint32_t rx_descriptor_ = 0u;
    uint32_t tx_descriptor_ = 0u;
};
