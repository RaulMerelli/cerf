#include "imx6_fec_legacy_ring.h"

#include "../../cpu/emulated_memory.h"
#include "../../net/network_backend.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kDescriptorStride = 8u;
constexpr uint32_t kMaximumDescriptors = 1024u;
constexpr uint32_t kDemandActive = 0x01000000u;

constexpr uint16_t kDescriptorOwned = 0x8000u;
constexpr uint16_t kDescriptorWrap = 0x2000u;
constexpr uint16_t kDescriptorLast = 0x0800u;

constexpr uint32_t kEirRxb = 0x01000000u;
constexpr uint32_t kEirRxf = 0x02000000u;
constexpr uint32_t kEirTxb = 0x04000000u;
constexpr uint32_t kEirTxf = 0x08000000u;

uint32_t EthernetFcs(const uint8_t* data, std::size_t length) {
    /* QEMU v9.2.2 hw/net/imx_fec.c: imx_fec_receive uses
       crc32(~0, frame, length), then exposes the big-endian result. */
    uint32_t crc = ~0u;
    crc = ~crc;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

uint32_t NextDescriptor(uint32_t current, uint16_t status, uint32_t base) {
    return (status & kDescriptorWrap) ? base : current + kDescriptorStride;
}

}  // namespace

void Imx6FecLegacyRing::Reset() {
    rdar_ = 0u;
    tdar_ = 0u;
    rx_descriptor_ = 0u;
    tx_descriptor_ = 0u;
}

void Imx6FecLegacyRing::SaveState(StateWriter& writer) const {
    writer.Write(rdar_);
    writer.Write(tdar_);
    writer.Write(rx_descriptor_);
    writer.Write(tx_descriptor_);
}

void Imx6FecLegacyRing::RestoreState(StateReader& reader) {
    reader.Read(rdar_);
    reader.Read(tdar_);
    reader.Read(rx_descriptor_);
    reader.Read(tx_descriptor_);
}

void Imx6FecLegacyRing::SetRxDescriptorBase(uint32_t base) {
    rx_descriptor_ = base;
}

void Imx6FecLegacyRing::SetTxDescriptorBase(uint32_t base) {
    tx_descriptor_ = base;
}

void Imx6FecLegacyRing::Disable(uint32_t rx_base, uint32_t tx_base) {
    rdar_ = 0u;
    tdar_ = 0u;
    rx_descriptor_ = rx_base;
    tx_descriptor_ = tx_base;
}

void Imx6FecLegacyRing::RequestReceive(EmulatedMemory& memory,
                                       bool controller_enabled) {
    if (!controller_enabled) {
        rdar_ = 0u;
        return;
    }
    RefreshReceiveDemand(memory);
}

void Imx6FecLegacyRing::RefreshReceiveDemand(EmulatedMemory& memory) {
    uint8_t* descriptor = memory.TryTranslateWrite(rx_descriptor_);
    uint16_t status = 0u;
    if (descriptor)
        std::memcpy(&status, descriptor + 2, sizeof(status));
    rdar_ = descriptor && (status & kDescriptorOwned) ? kDemandActive : 0u;
}

uint32_t Imx6FecLegacyRing::RequestTransmit(EmulatedMemory& memory,
                                            NetworkBackend& network,
                                            bool controller_enabled,
                                            bool cable_connected,
                                            uint32_t tx_base) {
    if (!controller_enabled || tx_base == 0u) {
        tdar_ = 0u;
        return 0u;
    }

    tdar_ = kDemandActive;
    uint32_t events = 0u;
    std::vector<uint8_t> frame;
    frame.reserve(1518u);

    for (uint32_t count = 0u; count < kMaximumDescriptors; ++count) {
        uint8_t* descriptor = memory.TryTranslateWrite(tx_descriptor_);
        if (!descriptor)
            break;

        uint16_t length = 0u;
        uint16_t status = 0u;
        uint32_t data_address = 0u;
        std::memcpy(&length, descriptor, sizeof(length));
        std::memcpy(&status, descriptor + 2, sizeof(status));
        std::memcpy(&data_address, descriptor + 4, sizeof(data_address));
        if ((status & kDescriptorOwned) == 0u)
            break;

        const std::size_t room = 1518u - std::min<std::size_t>(1518u, frame.size());
        const std::size_t copy_length = std::min<std::size_t>(length, room);
        if (copy_length != 0u) {
            const uint8_t* data = memory.TryTranslateWrite(data_address);
            if (!data)
                break;
            frame.insert(frame.end(), data, data + copy_length);
        }

        const bool last = (status & kDescriptorLast) != 0u;
        status &= static_cast<uint16_t>(~kDescriptorOwned);
        std::memcpy(descriptor + 2, &status, sizeof(status));
        events |= kEirTxb;
        tx_descriptor_ = NextDescriptor(tx_descriptor_, status, tx_base);

        if (last) {
            if (cable_connected && !frame.empty())
                network.SendFrame(frame.data(), frame.size());
            frame.clear();
            events |= kEirTxf;
        }
    }

    tdar_ = 0u;
    return events;
}

uint32_t Imx6FecLegacyRing::Receive(EmulatedMemory& memory,
                                    const uint8_t* frame,
                                    std::size_t length,
                                    uint32_t rx_base,
                                    uint32_t max_receive_buffer) {
    if (rdar_ == 0u || max_receive_buffer == 0u)
        return 0u;

    /* i.MX6DQRM section 23.6.13 and QEMU imx_fec_receive: legacy receive
       descriptor lengths include the four-byte FCS delivered after payload. */
    std::vector<uint8_t> packet(frame, frame + length);
    const uint32_t crc = EthernetFcs(frame, length);
    packet.push_back(static_cast<uint8_t>(crc >> 24));
    packet.push_back(static_cast<uint8_t>(crc >> 16));
    packet.push_back(static_cast<uint8_t>(crc >> 8));
    packet.push_back(static_cast<uint8_t>(crc));

    uint32_t events = 0u;
    std::size_t offset = 0u;
    for (uint32_t count = 0u;
         count < kMaximumDescriptors && offset < packet.size();
         ++count) {
        uint8_t* descriptor = memory.TryTranslateWrite(rx_descriptor_);
        if (!descriptor)
            break;

        uint16_t status = 0u;
        uint32_t data_address = 0u;
        std::memcpy(&status, descriptor + 2, sizeof(status));
        std::memcpy(&data_address, descriptor + 4, sizeof(data_address));
        if ((status & kDescriptorOwned) == 0u)
            break;

        uint8_t* destination = memory.TryTranslateWrite(data_address);
        if (!destination)
            break;
        const std::size_t copy_length = std::min<std::size_t>(
            packet.size() - offset, max_receive_buffer);
        std::memcpy(destination, packet.data() + offset, copy_length);
        offset += copy_length;

        const bool last = offset == packet.size();
        const uint16_t descriptor_length = static_cast<uint16_t>(copy_length);
        /* QEMU v9.2.2 hw/net/imx_fec.c:imx_fec_receive clears only ENET_BD_E
           and adds ENET_BD_L on the final descriptor.  KTP400 enet.dll
           sub_EF4B45F4 additionally requires its software-owned 0x4000 marker
           to survive completion before it indicates the frame to NDIS. */
        status &= static_cast<uint16_t>(~kDescriptorOwned);
        if (last)
            status |= kDescriptorLast;
        std::memcpy(descriptor, &descriptor_length, sizeof(descriptor_length));
        std::memcpy(descriptor + 2, &status, sizeof(status));
        events |= last ? kEirRxf : kEirRxb;
        rx_descriptor_ = NextDescriptor(rx_descriptor_, status, rx_base);
    }

    RefreshReceiveDemand(memory);
    return events;
}
