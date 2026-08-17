#pragma once

#include <cstdint>

namespace imx6_ipu {

/* Linux drivers/gpu/ipu-v3/ipu-prv.h: i.MX6 IPUv3 register layout. */
constexpr uint32_t kBase = 0x02400000u;
constexpr uint32_t kSize = 0x00300000u;
constexpr uint32_t kOffConf = 0x00000000u;
constexpr uint32_t kConfDpEn = 1u << 5;
constexpr uint32_t kConfDi0En = 1u << 6;
constexpr uint32_t kConfDi1En = 1u << 7;
constexpr uint32_t kConfDcEn = 1u << 9;
constexpr uint32_t kConfDmfcEn = 1u << 10;
constexpr uint32_t kConfVdiEn = 1u << 12;
constexpr uint32_t kConfIdmacDis = 1u << 22;
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
constexpr uint32_t kOffIdmacChPri1 = 0x00008014u;
constexpr uint32_t kOffIdmacBusy1 = 0x00008100u;
constexpr uint32_t kOffIdmacBusy2 = 0x00008104u;
constexpr uint32_t kOffDpBase = 0x00018000u;
constexpr uint32_t kOffDpEnd = 0x00018400u;
constexpr uint32_t kOffIcBase = 0x00020000u;
constexpr uint32_t kOffIcEnd = 0x00020200u;
constexpr uint32_t kIcConf = 0x00000000u;
constexpr uint32_t kIcPrpEncRsc = 0x00000004u;
constexpr uint32_t kIcPrpVfRsc = 0x00000008u;
constexpr uint32_t kIcPpRsc = 0x0000000Cu;
constexpr uint32_t kIcCmbp1 = 0x00000010u;
constexpr uint32_t kIcCmbp2 = 0x00000014u;
constexpr uint32_t kIcIdmac1 = 0x00000018u;
constexpr uint32_t kIcIdmac2 = 0x0000001Cu;
constexpr uint32_t kIcIdmac3 = 0x00000020u;
constexpr uint32_t kIcIdmac4 = 0x00000024u;
constexpr uint32_t kIcConfRwsEn = 0x40000000u;
constexpr uint32_t kIcConfCsiMemWrEn = 0x80000000u;
constexpr uint32_t kOffIrtBase = 0x00028000u;
constexpr uint32_t kOffIrtEnd = 0x00028200u;
constexpr uint32_t kOffCsi0Base = 0x00030000u;
constexpr uint32_t kOffCsi1Base = 0x00038000u;
constexpr uint32_t kOffCsiSize = 0x00000200u;
constexpr uint32_t kOffSmfcBase = 0x00050000u;
constexpr uint32_t kOffSmfcEnd = 0x00050200u;
constexpr uint32_t kOffDcBase = 0x00058000u;
constexpr uint32_t kOffDcEnd = 0x00058200u;
constexpr uint32_t kDcChStride = 0x0000000Cu;
constexpr uint32_t kDcWrChConf = 0x00000000u;
constexpr uint32_t kDcGen = 0x000000D4u;
constexpr uint32_t kDcDispConf1_0 = 0x000000D8u;
constexpr uint32_t kDcDispConf2_0 = 0x000000E8u;
constexpr uint32_t kDcMapConfPtr0 = 0x00000108u;
constexpr uint32_t kDcMapConfVal0 = 0x00000144u;
constexpr uint32_t kDcStat = 0x000001C8u;
constexpr uint32_t kOffDmfcBase = 0x00060000u;
constexpr uint32_t kOffDmfcEnd = 0x00060040u;
constexpr uint32_t kDmfcRdChan = 0x00000000u;
constexpr uint32_t kDmfcWrChan = 0x00000004u;
constexpr uint32_t kDmfcWrChanDef = 0x00000008u;
constexpr uint32_t kDmfcDpChan = 0x0000000Cu;
constexpr uint32_t kDmfcDpChanDef = 0x00000010u;
constexpr uint32_t kDmfcGeneral1 = 0x00000014u;
constexpr uint32_t kDmfcGeneral2 = 0x00000018u;
constexpr uint32_t kDmfcIcCtrl = 0x0000001Cu;
constexpr uint32_t kDmfcWrChanAlt = 0x00000020u;
constexpr uint32_t kDmfcWrChanDefAlt = 0x00000024u;
constexpr uint32_t kDmfcDpChanAlt = 0x00000028u;
constexpr uint32_t kDmfcDpChanDefAlt = 0x0000002Cu;
constexpr uint32_t kDmfcGeneral1Alt = 0x00000030u;
constexpr uint32_t kDmfcStat = 0x00000034u;
constexpr uint32_t kOffDi0Base = 0x00040000u;
constexpr uint32_t kOffDi1Base = 0x00042000u;
constexpr uint32_t kOffDiSize = 0x00000200u;
constexpr uint32_t kDiGeneral = 0x00000000u;
constexpr uint32_t kDiBsClkGen0 = 0x00000004u;
constexpr uint32_t kDiBsClkGen1 = 0x00000008u;
constexpr uint32_t kDiSyncAsGen = 0x00000054u;
constexpr uint32_t kDiPol = 0x00000164u;
constexpr uint32_t kDiStat = 0x00000174u;
/* Linux drivers/gpu/ipu-v3/ipu-common.c and ipu-vdi.c: i.MX6Q/DL VDI block. */
constexpr uint32_t kOffVdiBase = 0x00068000u;
constexpr uint32_t kOffVdiEnd = 0x00069000u;
constexpr uint32_t kVdiFsize = 0x00000000u;
constexpr uint32_t kVdiControl = 0x00000004u;
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
    return offset == kOffChaBuf0Rdy0 || offset == kOffChaBuf0Rdy0 + 4u ||
           offset == kOffChaBuf1Rdy0 || offset == kOffChaBuf1Rdy0 + 4u;
}
inline bool IsDcOff(uint32_t offset) {
    return offset >= kOffDcBase && offset < kOffDcEnd;
}
inline bool IsDmfcOff(uint32_t offset) {
    return offset >= kOffDmfcBase && offset < kOffDmfcEnd;
}
inline bool IsDpOff(uint32_t offset) {
    return offset >= kOffDpBase && offset < kOffDpEnd;
}
inline bool IsIcOff(uint32_t offset) {
    return offset >= kOffIcBase && offset < kOffIcEnd;
}
inline bool IsIrtOff(uint32_t offset) {
    return offset >= kOffIrtBase && offset < kOffIrtEnd;
}
inline bool IsCsiOff(uint32_t offset) {
    return (offset >= kOffCsi0Base && offset < kOffCsi0Base + kOffCsiSize) ||
           (offset >= kOffCsi1Base && offset < kOffCsi1Base + kOffCsiSize);
}
inline bool IsSmfcOff(uint32_t offset) {
    return offset >= kOffSmfcBase && offset < kOffSmfcEnd;
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

inline bool IsModelledRegister(uint32_t offset) {
    if (offset == kOffConf || offset == kOffFsDispFlow1 ||
        offset == kOffFsDispFlow2 || offset == kOffDispGen ||
        offset == kOffMemRst || IsIpuIntCtrl(offset) ||
        IsIpuIntStat(offset) || IsIpuCurBuf(offset) || IsIpuBufReady(offset))
        return true;
    if (offset >= 0x000000A0u && offset <= 0x000000E4u) return true;
    if (offset >= 0x00000150u && offset <= 0x00000288u) return true;
    if (offset >= 0x00008000u && offset < 0x00008108u) return true;
    return IsIcOff(offset) || IsDcOff(offset) || IsDmfcOff(offset) ||
           IsDiOff(offset) || IsDpOff(offset) || IsVdiOff(offset);
}

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

inline const char* BlockName(uint32_t offset) {
    if (offset < 0x00001000u) return "CM";
    if (offset >= 0x00008000u && offset < 0x00008200u) return "IDMAC";
    if (IsDpOff(offset)) return "DP";
    if (IsIcOff(offset)) return "IC";
    if (IsIrtOff(offset)) return "IRT";
    if (IsCsiOff(offset)) return "CSI";
    if (IsSmfcOff(offset)) return "SMFC";
    if (IsDcOff(offset)) return "DC";
    if (IsDmfcOff(offset)) return "DMFC";
    if (IsDiOff(offset)) return "DI";
    if (IsVdiOff(offset)) return "VDI";
    return "IPU-unknown";
}

inline const char* RegName(uint32_t offset) {
    switch (offset) {
        case kOffConf: return "IPU_CONF";
        case kOffFsDispFlow1: return "IPU_FS_DISP_FLOW1";
        case kOffFsDispFlow2: return "IPU_FS_DISP_FLOW2";
        case kOffDispGen: return "IPU_DISP_GEN";
        case kOffMemRst: return "IPU_MEM_RST";
        case kOffIdmacChEn1: return "IDMAC_CHA_EN_1";
        case kOffIdmacChEn2: return "IDMAC_CHA_EN_2";
        case kOffIdmacChPri1: return "IDMAC_CHA_PRI_1";
        case kOffIdmacBusy1: return "IDMAC_CHA_BUSY_1";
        case kOffIdmacBusy2: return "IDMAC_CHA_BUSY_2";
        case kOffChaCurBuf0: return "IPU_CHA_CUR_BUF_1";
        case kOffChaBuf0Rdy0: return "IPU_CHA_BUF0_RDY_1";
        case kOffChaBuf1Rdy0: return "IPU_CHA_BUF1_RDY_1";
        default: break;
    }
    if (IsDcOff(offset)) {
        switch (offset - kOffDcBase) {
            case kDcGen: return "IPU_DC_GEN";
            case kDcDispConf1_0: return "IPU_DC_DISP_CONF1_0";
            case kDcDispConf2_0: return "IPU_DC_DISP_CONF2_0";
            case kDcMapConfPtr0: return "IPU_DC_MAP_CONF_PTR0";
            case kDcMapConfVal0: return "IPU_DC_MAP_CONF_VAL0";
            case kDcStat: return "IPU_DC_STAT";
            default: return "IPU_DC";
        }
    }
    if (IsDmfcOff(offset)) {
        switch (offset - kOffDmfcBase) {
            case kDmfcRdChan: return "IPU_DMFC_RD_CHAN";
            case kDmfcWrChan: return "IPU_DMFC_WR_CHAN";
            case kDmfcWrChanDef: return "IPU_DMFC_WR_CHAN_DEF";
            case kDmfcDpChan: return "IPU_DMFC_DP_CHAN";
            case kDmfcDpChanDef: return "IPU_DMFC_DP_CHAN_DEF";
            case kDmfcGeneral1: return "IPU_DMFC_GENERAL1";
            case kDmfcGeneral2: return "IPU_DMFC_GENERAL2";
            case kDmfcIcCtrl: return "IPU_DMFC_IC_CTRL";
            case kDmfcWrChanAlt: return "IPU_DMFC_WR_CHAN_ALT";
            case kDmfcWrChanDefAlt: return "IPU_DMFC_WR_CHAN_DEF_ALT";
            case kDmfcDpChanAlt: return "IPU_DMFC_DP_CHAN_ALT";
            case kDmfcDpChanDefAlt: return "IPU_DMFC_DP_CHAN_DEF_ALT";
            case kDmfcGeneral1Alt: return "IPU_DMFC_GENERAL1_ALT";
            case kDmfcStat: return "IPU_DMFC_STAT";
            default: return "IPU_DMFC";
        }
    }
    if (IsDpOff(offset)) return "IPU_DP";
    if (IsIcOff(offset)) {
        switch (offset - kOffIcBase) {
            case kIcConf: return "IPU_IC_CONF";
            case kIcPrpEncRsc: return "IPU_IC_PRP_ENC_RSC";
            case kIcPrpVfRsc: return "IPU_IC_PRP_VF_RSC";
            case kIcPpRsc: return "IPU_IC_PP_RSC";
            case kIcCmbp1: return "IPU_IC_CMBP_1";
            case kIcCmbp2: return "IPU_IC_CMBP_2";
            case kIcIdmac1: return "IPU_IC_IDMAC_1";
            case kIcIdmac2: return "IPU_IC_IDMAC_2";
            case kIcIdmac3: return "IPU_IC_IDMAC_3";
            case kIcIdmac4: return "IPU_IC_IDMAC_4";
            default: return "IPU_IC";
        }
    }
    if (IsIrtOff(offset)) return "IPU_IRT";
    if (IsCsiOff(offset)) return "IPU_CSI";
    if (IsSmfcOff(offset)) return "IPU_SMFC";
    if (IsDiOff(offset)) {
        switch (offset & 0x1FFFu) {
            case kDiGeneral: return "IPU_DI_GENERAL";
            case kDiBsClkGen0: return "IPU_DI_BS_CLKGEN0";
            case kDiBsClkGen1: return "IPU_DI_BS_CLKGEN1";
            case kDiSyncAsGen: return "IPU_DI_SYNC_AS_GEN";
            case kDiPol: return "IPU_DI_POL";
            case kDiStat: return "IPU_DI_STAT";
            default: return "IPU_DI";
        }
    }
    if (IsVdiOff(offset)) {
        if (offset - kOffVdiBase == kVdiFsize) return "IPU_VDI_FSIZE";
        if (offset - kOffVdiBase == kVdiControl) return "IPU_VDI_C";
        return "IPU_VDI";
    }
    return "";
}

}
