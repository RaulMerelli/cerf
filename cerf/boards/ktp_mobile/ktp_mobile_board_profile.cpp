#include "ktp_mobile_board_profile.h"

#include "../../core/log.h"
#include "ktp_mobile_id.h"

namespace {

/* Resolutions: Siemens data sheets 6AV2125-2DB23-0AX0 (4.3 in, 480 x 272),
   6AV2125-2GB23-0AX0 and 6AV2125-2JB23-0AX0 (7 in and 9 in, 800 x 480) and
   6AV2145-6KB20-0AS0 (10.1 in, 1280 x 800), page 1 of each. */

/* No panel's own blanking is in any reference on hand. ddraw_ipu.dll sub_EF52245C
   copies these values into its configuration without validating them, and nothing in
   CERF reads them, so each set is a stand-in whose clock gives 60 Hz over its totals. */
/* Fields: width, height, hsync, hstart, hend, vsync, vstart, vend, pixel clock, bus. */
constexpr KtpMobilePanel kPanel480x272AbsentStub =
    {480u, 272u, 1u, 42u, 8u, 10u, 2u, 4u, 9175680u, 24u};
constexpr KtpMobilePanel kPanel800x480AbsentStub =
    {800u, 480u, 96u, 128u, 32u, 2u, 32u, 11u, 33264000u, 24u};
constexpr KtpMobilePanel kPanel1280x800AbsentStub =
    {1280u, 800u, 136u, 200u, 64u, 3u, 24u, 1u, 83462400u, 24u};

constexpr KtpMobileBoardProfile kProfiles[] = {
    {BoardId::HmiKtp400FMobile,       KtpMobileOpType::Ktp400F,       kPanel480x272AbsentStub, "4in",   true},
    {BoardId::HmiKtp700Mobile,        KtpMobileOpType::Ktp700,        kPanel800x480AbsentStub, "7_9in", false},
    {BoardId::HmiKtp700FMobile,       KtpMobileOpType::Ktp700F,       kPanel800x480AbsentStub, "7_9in", true},
    {BoardId::HmiKtp900Mobile,        KtpMobileOpType::Ktp900,        kPanel800x480AbsentStub, "7_9in", false},
    {BoardId::HmiKtp900FMobile,       KtpMobileOpType::Ktp900F,       kPanel800x480AbsentStub, "7_9in", true},
    {BoardId::HmiTp1000fMobile,       KtpMobileOpType::Tp1000F,       kPanel1280x800AbsentStub, "10in", true},
    {BoardId::HmiKtp700FHwMobile,     KtpMobileOpType::Ktp700FHw,     kPanel800x480AbsentStub, "7_9in", true},
    {BoardId::HmiKtp700FArcticMobile, KtpMobileOpType::Ktp700FArctic, kPanel800x480AbsentStub, "7_9in", true},
};

}

const KtpMobileBoardProfile* TryKtpMobileBoardProfileFor(std::string_view board) {
    for (const auto& profile : kProfiles)
        if (profile.board == board) return &profile;
    return nullptr;
}

const KtpMobileBoardProfile& KtpMobileBoardProfileFor(std::string_view board) {
    if (const auto* profile = TryKtpMobileBoardProfileFor(board)) return *profile;
    LOG(Caution, "KtpMobileBoardProfile: unsupported board %.*s\n", static_cast<int>(board.size()), board.data());
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}
