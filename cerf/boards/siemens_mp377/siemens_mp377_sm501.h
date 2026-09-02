#pragma once

#include "siemens_mp377_panel.h"

#include <cstdint>

namespace siemens_mp377 {

/* Guest-visible SM501 PCI apertures.

   The model must not treat these physical addresses as the internal SM501
   identity.  BAR0 and BAR1 are bus windows only.  All SM501 register/video
   logic must operate on BAR-relative offsets: guest BAR0 + off -> VRAM[off],
   guest BAR1 + off -> SM501 register offset off.

   Keep the MP377 boot-stable addresses here while refactoring the rest of the
   device to consume offsets instead of absolute PA values. */
inline constexpr uint32_t kSm501FbBarBus = 0xCA000000u;
inline constexpr uint32_t kSm501FbBytes = 0x01000000u;
inline constexpr uint32_t kSm501RegsBarBus = 0xC8000000u;
inline constexpr uint32_t kSm501RegsBytes = 0x00200000u;

/* Keep the SM501 device model's native BAR-relative decode at the PCI bus
   apertures.  The MP377 OAL maps the ATU-primary outbound window at CPU PA
   C0000000-C7FFFFFF; SM501 register/video accesses that arrive through that
   CPU-static window are forwarded by the board IO bridge below. */
inline constexpr uint32_t kSm501RegsBarPa = kSm501RegsBarBus;
inline constexpr uint32_t kSm501FbBarPa = kSm501FbBarBus;
inline constexpr uint32_t kSm501RegsCpuStaticPa = 0xC0000000u;
inline constexpr uint32_t kSm501FbCpuStaticPa = 0xC2000000u;

inline constexpr bool Sm501OffsetInRange(uint32_t off, uint32_t bytes) {
    return off < bytes;
}

inline bool Sm501BusToOffset(uint32_t pa, uint32_t base, uint32_t bytes, uint32_t& off) {
    if (pa < base) {
        return false;
    }
    off = pa - base;
    return off < bytes;
}

inline bool Sm501FbPaToOffset(uint32_t pa, uint32_t& off) {
    return Sm501BusToOffset(pa, kSm501FbBarPa, kSm501FbBytes, off);
}

inline bool Sm501RegsPaToOffset(uint32_t pa, uint32_t& off) {
    return Sm501BusToOffset(pa, kSm501RegsBarPa, kSm501RegsBytes, off);
}

inline constexpr uint32_t Sm501FbOffsetToPa(uint32_t off) {
    return kSm501FbBarPa + off;
}

inline constexpr uint32_t Sm501RegsOffsetToPa(uint32_t off) {
    return kSm501RegsBarPa + off;
}

static_assert(Sm501FbOffsetToPa(0u) == kSm501FbBarPa, "SM501 BAR0 helper mismatch");
static_assert(Sm501RegsOffsetToPa(0u) == kSm501RegsBarPa, "SM501 BAR1 helper mismatch");

inline constexpr uint32_t kSm501PciVendorId = 0x126Fu;
inline constexpr uint32_t kSm501PciDeviceId = 0x0501u;
inline constexpr uint32_t kSm501PciDeviceVendorDword = (kSm501PciDeviceId << 16) | kSm501PciVendorId;
/* Design-guide PCI header value after board firmware enables I/O, memory, bus-master,
   special cycles and memory-write-invalidate: Status=0x0230, Command=0x001E.
   The MP377 CE driver stack probes this board-ready config space, not a cold PCI reset device.
   CSR34 is reserved by the SM501 design-guide type-00 header, so do not advertise
   a PCI capability list here. */
inline constexpr uint32_t kSm501PciCommandStatusDword = 0x0230001Eu;
/* PCI class register dword layout: class[31:24], subclass[23:16],
   prog-if[15:8], revision[7:0].  MP377's PCI WaveDev template matches
   SM501 as Class=0x03/SubClass=0x80 and loads VGXaudio.dll through
   PCIbus.dll.  smibase.dll accepts either subclass 0x00 or 0x80 for
   display bring-up, so advertising the real multimedia-display subclass
   keeps ddi_vgx/smibase compatible while allowing WaveDev enumeration. */
inline constexpr uint32_t kSm501PciClassDisplayDword = 0x038000A0u;
inline constexpr uint32_t kSm501PciHeaderTypeDword = 0x00000000u;
/* siemens_mp377_v1040 smibase.dll sub_2B51544 stores the BAR1 config resource and sub_2B5456C
   returns it through IOCTL 11.  VGXaudio passes that value to CEDDK
   TransBusAddrToStatic; siemens_mp377_v1040 nk.exe sub_80447F90/sub_80448ACC leaves PCI
   memory addresses unchanged, so the resource must be the CPU-static PA
   covered by the OAT.  The SM501 device still decodes its native C8000000
   bus aperture; the board bridge translates C0000000 accesses to it. */
inline constexpr uint32_t kSm501PciFbBarFlags = 0x00000000u;
inline constexpr uint32_t kSm501PciRegsBarFlags = 0x00000000u;
inline constexpr uint32_t kSm501PciFbBarDword = kSm501FbBarPa | kSm501PciFbBarFlags;
inline constexpr uint32_t kSm501PciRegsBarDword = kSm501RegsCpuStaticPa | kSm501PciRegsBarFlags;
inline constexpr uint32_t kSm501PciFbBarSizeMask = ~(kSm501FbBytes - 1u);
inline constexpr uint32_t kSm501PciRegsBarSizeMask = ~(kSm501RegsBytes - 1u);
inline constexpr uint32_t kSm501PciInterruptPinIntaLine0Dword = 0x0000010Au;
inline constexpr uint32_t kSm501PciSubsystemSiemensDword = 0x01010101u;
inline constexpr uint32_t kSm501PciCapabilityPointerDword = 0x00000000u; /* CSR34 reserved on SM501 PCI header */

enum class Mp377SmiBridgeWindowId {
    C410,
    C480,
};

inline constexpr uint32_t kSmiBridgeWindowBytes = 0x00000010u;

inline constexpr uint32_t SmiBridgeBase(Mp377SmiBridgeWindowId id) {
    return id == Mp377SmiBridgeWindowId::C410 ? 0xC4100028u : 0xC4800028u;
}

inline constexpr uint32_t SmiBridgeEnd(Mp377SmiBridgeWindowId id) {
    return SmiBridgeBase(id) + kSmiBridgeWindowBytes;
}

inline constexpr uint32_t kFbWidth = kMp377HwiPanel.width;
inline constexpr uint32_t kFbHeight = kMp377HwiPanel.height;
inline constexpr uint32_t kFbStride = kFbWidth * (kMp377HwiPanel.bpp / 8u);

} // namespace siemens_mp377
