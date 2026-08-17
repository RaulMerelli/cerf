#pragma once

#include <cstdint>

/* QEMU references/qemu/hw_sd/sdhci-internal.h and i.MX6SDLRM section 67.8. */
inline constexpr uint32_t kDS_ADDR = 0x00u;
inline constexpr uint32_t kBLK_ATT = 0x04u;
inline constexpr uint32_t kCMD_ARG = 0x08u;
inline constexpr uint32_t kCMD_XFR_TYP = 0x0Cu;
inline constexpr uint32_t kCMD_RSP0 = 0x10u;
inline constexpr uint32_t kCMD_RSP1 = 0x14u;
inline constexpr uint32_t kCMD_RSP2 = 0x18u;
inline constexpr uint32_t kCMD_RSP3 = 0x1Cu;
inline constexpr uint32_t kDATA_BUFF = 0x20u;
inline constexpr uint32_t kPRES_STATE = 0x24u;
inline constexpr uint32_t kPROT_CTRL = 0x28u;
inline constexpr uint32_t kSYS_CTRL = 0x2Cu;
inline constexpr uint32_t kIRQSTAT = 0x30u;
inline constexpr uint32_t kIRQSTATEN = 0x34u;
inline constexpr uint32_t kIRQSIGEN = 0x38u;
inline constexpr uint32_t kAUTOCMD12 = 0x3Cu;
inline constexpr uint32_t kHOST_CAP = 0x40u;
inline constexpr uint32_t kWTMK_LVL = 0x44u;
inline constexpr uint32_t kMIX_CTRL = 0x48u;
inline constexpr uint32_t kADMA_SYS_ADDR = 0x58u;
inline constexpr uint32_t kVEND_SPEC = 0xC0u;
inline constexpr uint32_t kHOST_VER = 0xFCu;

inline constexpr uint32_t kCC = 0x00000001u;
inline constexpr uint32_t kTC = 0x00000002u;
inline constexpr uint32_t kDINT = 0x00000008u;
inline constexpr uint32_t kBWR = 0x00000010u;
inline constexpr uint32_t kBRR = 0x00000020u;
inline constexpr uint32_t kERRI = 0x00008000u;
inline constexpr uint32_t kCTOE = 0x00010000u;
inline constexpr uint32_t kErrorSpecificMask = 0x117F0000u;

inline constexpr uint32_t kCLK_INT_EN = 0x00000001u;
inline constexpr uint32_t kCLK_INT_STBL = 0x00000002u;
inline constexpr uint32_t kINITA = 0x08000000u;

inline constexpr uint32_t kPS_CMD_INH = 0x00000001u;
inline constexpr uint32_t kPS_DAT_INH = 0x00000002u;
inline constexpr uint32_t kPS_DLA = 0x00000004u;
inline constexpr uint32_t kPS_CLK_STBL = 0x00000008u;
inline constexpr uint32_t kPS_DWR = 0x00000100u;
inline constexpr uint32_t kPS_DRD = 0x00000200u;
inline constexpr uint32_t kPS_BUF_SPC = 0x00000400u;
inline constexpr uint32_t kPS_BUF_RDY = 0x00000800u;
inline constexpr uint32_t kPS_CARD_PRES = 0x00010000u;
inline constexpr uint32_t kPS_CARD_DET = 0x00040000u;
inline constexpr uint32_t kPS_WP_LVL = 0x00080000u;
inline constexpr uint32_t kPS_DAT_IDLE = 0xFF000000u;
inline constexpr uint32_t kPS_CMD_LVL = 0x00800000u;

/* QEMU hw/arm/fsl-imx6.c and references/qemu/hw_sd/sdhci.c. */
inline constexpr uint32_t kCapabilities = 0x057834B4u;
inline constexpr uint32_t kHostVersion = 0x24010000u;

/* Linux references/sources/linux/sdhci-esdhc-imx.c. */
inline constexpr uint32_t kMixDmaEn = 0x00000001u;
inline constexpr uint32_t kMixBlkCntEn = 0x00000002u;
inline constexpr uint32_t kMixAutoCmd12 = 0x00000004u;
inline constexpr uint32_t kMixDataRead = 0x00000010u;
inline constexpr uint32_t kMixMultiBlk = 0x00000020u;
