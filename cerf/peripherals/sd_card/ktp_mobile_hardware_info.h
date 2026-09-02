#pragma once

#include <array>
#include <cstdint>
#include <vector>

/* Device-type bits of the Mobile Panel line, from the bspio lookup table each
   ROM carries (bspio\initialize.cpp:244).  Each table entry is
   { 0x40, bit, TransferName, DeviceName, BackupName } and the selector
   compares the pair returned by HWI_GetOPTypeEx against it.  The V13 images
   carry the first five entries; the V17 ones carry all nine. */
enum class KtpMobileOpType : uint16_t {
    Ktp400F = 0x0001,
    Ktp700 = 0x0002,
    Ktp700F = 0x0004,
    Ktp900 = 0x0008,
    Ktp900F = 0x0010,
    Tp1000F = 0x0040,
    Tp1000FRo = 0x0080,
    Ktp700FHw = 0x0100,
    Ktp700FArctic = 0x0200,
};

/* Panel geometry the hardware-info tree publishes, in pixels. */
struct KtpMobilePanel {
    uint16_t width;
    uint16_t height;
};

std::vector<uint8_t> BuildKtpMobileHardwareInfoOms(const std::array<uint8_t, 6>& mac, KtpMobileOpType op_type,
                                                   KtpMobilePanel panel);

/* DeviceManager's installed /hwdesc accepts the HardwareDescription root and
   the board identity/network branches.  Display geometry is supplied through
   the OAL/BSPIO handoff above, whose schema additionally contains MMI data. */
std::vector<uint8_t> BuildKtpMobileInstalledHardwareDescriptionOms(const std::array<uint8_t, 6>& mac,
                                                                   KtpMobileOpType op_type);
