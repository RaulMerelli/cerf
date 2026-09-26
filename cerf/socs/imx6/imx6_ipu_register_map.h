#pragma once

#include <cstdint>

namespace imx6_ipu {

/* Linux drivers/gpu/ipu-v3/ipu-prv.h: i.MX6 IPUv3 register layout. */
constexpr uint32_t kBase = 0x02400000u;
constexpr uint32_t kSize = 0x00300000u;
constexpr uint32_t kOffConf = 0x00000000u;
constexpr uint32_t kOffFsDispFlow1 = 0x000000B4u;
constexpr uint32_t kOffFsDispFlow2 = 0x000000B8u;
constexpr uint32_t kOffDispGen = 0x000000C4u;
constexpr uint32_t kOffMemRst = 0x000000DCu;
constexpr uint32_t kRstMemStart = 1u << 31;
constexpr uint32_t kOffIntCtrl0 = 0x0000003Cu;
constexpr uint32_t kOffIntStat0 = 0x00000200u;
constexpr uint32_t kOffChaCurBuf0 = 0x0000023Cu;
constexpr uint32_t kOffChaBuf0Rdy0 = 0x00000268u;
constexpr uint32_t kOffChaBuf1Rdy0 = 0x00000270u;
constexpr uint32_t kOffIdmacChEn1 = 0x00008004u;
constexpr uint32_t kOffIdmacChEn2 = 0x00008008u;
constexpr uint32_t kOffIdmacBusy1 = 0x00008100u;
constexpr uint32_t kOffIdmacBusy2 = 0x00008104u;
constexpr uint32_t kOffIcBase = 0x00020000u;
constexpr uint32_t kOffIcEnd = 0x00020200u;
constexpr uint32_t kOffDcBase = 0x00058000u;
constexpr uint32_t kOffDcEnd = 0x00058200u;
constexpr uint32_t kDcStat = 0x000001C8u;
constexpr uint32_t kOffDmfcBase = 0x00060000u;
constexpr uint32_t kOffDmfcEnd = 0x00060040u;
constexpr uint32_t kDmfcWrChan = 0x00000004u;
constexpr uint32_t kDmfcWrChanDef = 0x00000008u;
constexpr uint32_t kDmfcDpChan = 0x0000000Cu;
constexpr uint32_t kDmfcDpChanDef = 0x00000010u;
constexpr uint32_t kDmfcGeneral1 = 0x00000014u;
constexpr uint32_t kDmfcStat = 0x00000034u;
constexpr uint32_t kOffDi0Base = 0x00040000u;
constexpr uint32_t kOffDi1Base = 0x00042000u;
constexpr uint32_t kOffDiSize = 0x00000200u;
constexpr uint32_t kDiStat = 0x00000174u;
/* Linux drivers/gpu/ipu-v3/ipu-common.c and ipu-vdi.c: i.MX6Q/DL VDI block. */
constexpr uint32_t kOffVdiBase = 0x00068000u;
constexpr uint32_t kOffVdiEnd = 0x00069000u;
constexpr uint32_t kDisplayChannels[] = {23u, 24u, 27u, 28u, 29u, 41u, 42u, 43u};
constexpr int kIpuSyncSpi = 6;
constexpr int kIpuErrSpi = 5;
constexpr uint32_t kIpuIrqVsyncPre0 = 448u + 14u;

inline bool IsIpuIntCtrl(uint32_t offset) {
    return offset >= kOffIntCtrl0 && offset < kOffIntCtrl0 + 15u * 4u;
}
inline bool IsIpuIntStat(uint32_t offset) {
    return offset >= kOffIntStat0 && offset < kOffIntStat0 + 15u * 4u;
}
inline bool IsIpuCurBuf(uint32_t offset) {
    return offset == kOffChaCurBuf0 || offset == kOffChaCurBuf0 + 4u;
}
inline bool IsIpuBufReady(uint32_t offset) {
    return offset == kOffChaBuf0Rdy0 || offset == kOffChaBuf0Rdy0 + 4u || offset == kOffChaBuf1Rdy0 ||
           offset == kOffChaBuf1Rdy0 + 4u;
}
inline bool IsDcOff(uint32_t offset) {
    return offset >= kOffDcBase && offset < kOffDcEnd;
}
inline bool IsDmfcOff(uint32_t offset) {
    return offset >= kOffDmfcBase && offset < kOffDmfcEnd;
}
inline bool IsIcOff(uint32_t offset) {
    return offset >= kOffIcBase && offset < kOffIcEnd;
}
inline bool IsDiOff(uint32_t offset) {
    return (offset >= kOffDi0Base && offset < kOffDi0Base + kOffDiSize) ||
           (offset >= kOffDi1Base && offset < kOffDi1Base + kOffDiSize);
}
inline bool IsVdiOff(uint32_t offset) {
    return offset >= kOffVdiBase && offset < kOffVdiEnd;
}
inline bool IsIdleStatusRegister(uint32_t offset) {
    return (IsDcOff(offset) && offset - kOffDcBase == kDcStat) ||
           (IsDmfcOff(offset) && offset - kOffDmfcBase == kDmfcStat) ||
           (IsDiOff(offset) && (offset & 0x1FFFu) == kDiStat);
}

inline bool IsMeasuredBlockRead(uint32_t offset) {
    switch (offset) {
    case 0x20000u: case 0x20018u:
    case 0x40004u: case 0x40008u: case 0x40054u: case 0x40148u: case 0x4014Cu: case 0x40150u:
    case 0x40164u: case 0x40170u:
    case 0x58024u: case 0x58028u: case 0x5802Cu: case 0x58030u: case 0x58034u: case 0x5805Cu:
    case 0x58064u: case 0x58068u: case 0x5806Cu: case 0x58070u: case 0x58074u: case 0x58108u:
    case 0x58144u: case 0x58148u:
    case 0x60004u: case 0x60008u: case 0x6000Cu: case 0x60010u: case 0x60014u:
    case 0x68004u: return true;
    default: return false;
    }
}

inline bool IsMeasuredBlockWrite(uint32_t offset) {
    switch (offset) {
    case 0x20024u:
    case 0x40000u: case 0x4000Cu: case 0x40010u: case 0x40014u: case 0x40018u: case 0x4001Cu:
    case 0x40030u: case 0x40034u: case 0x40038u: case 0x4003Cu: case 0x40040u: case 0x40064u:
    case 0x40094u: case 0x40124u:
    case 0x5801Cu: case 0x58020u: case 0x58060u: case 0x580D4u: case 0x580D8u: case 0x580E8u:
    case 0x58174u: case 0x58184u: case 0x58194u: case 0x581A4u:
    case 0x60000u: case 0x60018u: return true;
    default: return IsMeasuredBlockRead(offset);
    }
}

inline bool IsCoreRegister(uint32_t offset) {
    if (offset == kOffConf || offset == kOffFsDispFlow1 || offset == kOffFsDispFlow2 || offset == kOffDispGen ||
        offset == kOffMemRst || IsIpuIntCtrl(offset) || IsIpuIntStat(offset) || IsIpuCurBuf(offset) ||
        IsIpuBufReady(offset))
        return true;
    if (offset >= 0x000000A0u && offset <= 0x000000E4u) return true;
    if (offset >= 0x00000150u && offset <= 0x00000288u) return true;
    return offset >= 0x00008000u && offset < 0x00008108u;
}

inline bool IsModelledRead(uint32_t offset) { return IsCoreRegister(offset) || IsMeasuredBlockRead(offset); }

inline bool IsModelledWrite(uint32_t offset) { return IsCoreRegister(offset) || IsMeasuredBlockWrite(offset); }

inline uint32_t DisplayChannelMask() {
    uint32_t mask = 0;
    for (const uint32_t channel : kDisplayChannels)
        if (channel < 32u) mask |= 1u << channel;
    return mask;
}
inline uint32_t DisplayChannelMaskHigh() {
    uint32_t mask = 0;
    for (const uint32_t channel : kDisplayChannels)
        if (channel >= 32u) mask |= 1u << (channel - 32u);
    return mask;
}

}
