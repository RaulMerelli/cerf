#include "imx6_fec_legacy_ring.h"

#include "../../core/crc32.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../net/network_backend.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kDescriptorStride = 8u;
constexpr uint32_t kMaximumDescriptors = 1024u;
constexpr uint32_t kDemandActive = 0x01000000u;

constexpr uint16_t kDescriptorOwned = 0x8000u;
constexpr uint16_t kDescriptorWrap = 0x2000u;
constexpr uint16_t kDescriptorLast = 0x0800u;

/* IMX6DQRM Rev.2 §23.5: the legacy FEC carries a frame of up to 1518 bytes. */
constexpr std::size_t kMaximumFrameBytes = 1518u;

constexpr uint32_t kEirRxb = 0x01000000u;
constexpr uint32_t kEirRxf = 0x02000000u;
constexpr uint32_t kEirTxb = 0x04000000u;
constexpr uint32_t kEirTxf = 0x08000000u;

uint32_t NextDescriptor(uint32_t current, uint16_t status, uint32_t base) {
    return (status & kDescriptorWrap) ? base : current + kDescriptorStride;
}

uint8_t* RequireDescriptor(EmulatedMemory& memory, uint32_t pa) {
    if (uint8_t* host = memory.TryTranslateRange(pa, kDescriptorStride, true)) return host;
    LOG(Caution, "i.MX6 FEC: buffer descriptor at 0x%08X is not mapped for %u bytes\n",
        pa, kDescriptorStride);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

uint8_t* RequireBuffer(EmulatedMemory& memory, uint32_t pa, std::size_t bytes, bool write) {
    if (uint8_t* host = memory.TryTranslateRange(pa, bytes, write)) return host;
    LOG(Caution, "i.MX6 FEC: buffer 0x%08X+%zu is not mapped for %s\n", pa, bytes,
        write ? "write" : "read");
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

}

void Imx6FecLegacyRing::Reset() {
    rdar_ = 0u;
    tdar_ = 0u;
    rx_descriptor_ = 0u;
    tx_descriptor_ = 0u;
}

void Imx6FecLegacyRing::SaveState(StateWriter& writer) const {
    writer.Write("rdar", rdar_);
    writer.Write("tdar", tdar_);
    writer.Write("rx_descriptor", rx_descriptor_);
    writer.Write("tx_descriptor", tx_descriptor_);
}

void Imx6FecLegacyRing::RestoreState(StateReader& reader) {
    reader.Read("rdar", rdar_);
    reader.Read("tdar", tdar_);
    reader.Read("rx_descriptor", rx_descriptor_);
    reader.Read("tx_descriptor", tx_descriptor_);
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

void Imx6FecLegacyRing::RequestReceive(EmulatedMemory& memory, bool controller_enabled) {
    if (!controller_enabled) {
        rdar_ = 0u;
        return;
    }
    RefreshReceiveDemand(memory);
}

void Imx6FecLegacyRing::RefreshReceiveDemand(EmulatedMemory& memory) {
    uint8_t* descriptor = RequireDescriptor(memory, rx_descriptor_);
    uint16_t status = 0u;
    std::memcpy(&status, descriptor + 2, sizeof(status));
    rdar_ = (status & kDescriptorOwned) ? kDemandActive : 0u;
}

uint32_t Imx6FecLegacyRing::RequestTransmit(EmulatedMemory& memory, NetworkBackend& network, bool controller_enabled,
                                            bool cable_connected, uint32_t tx_base) {
    if (!controller_enabled || tx_base == 0u) {
        tdar_ = 0u;
        return 0u;
    }

    tdar_ = kDemandActive;
    uint32_t events = 0u;
    std::vector<uint8_t> frame;
    frame.reserve(kMaximumFrameBytes);

    for (uint32_t count = 0u; count < kMaximumDescriptors; ++count) {
        uint8_t* descriptor = RequireDescriptor(memory, tx_descriptor_);

        uint16_t length = 0u;
        uint16_t status = 0u;
        uint32_t data_address = 0u;
        std::memcpy(&length, descriptor, sizeof(length));
        std::memcpy(&status, descriptor + 2, sizeof(status));
        std::memcpy(&data_address, descriptor + 4, sizeof(data_address));
        if ((status & kDescriptorOwned) == 0u) break;

        if (frame.size() + length > kMaximumFrameBytes) {
            LOG(Caution, "i.MX6 FEC: a transmit frame of %zu bytes is longer than the "
                         "%u bytes this model carries\n",
                frame.size() + length, kMaximumFrameBytes);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        if (length != 0u) {
            const uint8_t* data = RequireBuffer(memory, data_address, length, false);
            frame.insert(frame.end(), data, data + length);
        }

        const bool last = (status & kDescriptorLast) != 0u;
        status &= static_cast<uint16_t>(~kDescriptorOwned);
        std::memcpy(descriptor + 2, &status, sizeof(status));
        events |= kEirTxb;
        tx_descriptor_ = NextDescriptor(tx_descriptor_, status, tx_base);

        if (last) {
            if (cable_connected && !frame.empty()) network.SendFrame(frame.data(), frame.size());
            frame.clear();
            events |= kEirTxf;
        }
    }

    tdar_ = 0u;
    return events;
}

uint32_t Imx6FecLegacyRing::Receive(EmulatedMemory& memory, const uint8_t* frame, std::size_t length, uint32_t rx_base,
                                    uint32_t max_receive_buffer) {
    if (rdar_ == 0u || max_receive_buffer == 0u) return 0u;

    /* IMX6DQRM Rev.2 Table 23-106. */
    std::vector<uint8_t> packet(frame, frame + length);
    const uint32_t crc = cerf::Crc32(frame, length);
    packet.push_back(static_cast<uint8_t>(crc));
    packet.push_back(static_cast<uint8_t>(crc >> 8));
    packet.push_back(static_cast<uint8_t>(crc >> 16));
    packet.push_back(static_cast<uint8_t>(crc >> 24));

    uint32_t events = 0u;
    std::size_t offset = 0u;
    for (uint32_t count = 0u; count < kMaximumDescriptors && offset < packet.size(); ++count) {
        uint8_t* descriptor = RequireDescriptor(memory, rx_descriptor_);

        uint16_t status = 0u;
        uint32_t data_address = 0u;
        std::memcpy(&status, descriptor + 2, sizeof(status));
        std::memcpy(&data_address, descriptor + 4, sizeof(data_address));
        if ((status & kDescriptorOwned) == 0u) break;

        const std::size_t copy_length = std::min<std::size_t>(packet.size() - offset, max_receive_buffer);
        uint8_t* destination = RequireBuffer(memory, data_address, copy_length, true);
        std::memcpy(destination, packet.data() + offset, copy_length);
        offset += copy_length;

        const bool last = offset == packet.size();
        const uint16_t descriptor_length = static_cast<uint16_t>(copy_length);
        /* QEMU i.MX FEC model (imx_fec_receive) clears only ENET_BD_E and sets ENET_BD_L on the
           last descriptor; hmi_ktp400_mobile_v13 enet.dll sub_EF4B45F4 needs the 0x4000 marker kept. */
        status &= static_cast<uint16_t>(~kDescriptorOwned);
        if (last) status |= kDescriptorLast;
        std::memcpy(descriptor, &descriptor_length, sizeof(descriptor_length));
        std::memcpy(descriptor + 2, &status, sizeof(status));
        events |= last ? kEirRxf : kEirRxb;
        rx_descriptor_ = NextDescriptor(rx_descriptor_, status, rx_base);
    }

    RefreshReceiveDemand(memory);
    return events;
}
