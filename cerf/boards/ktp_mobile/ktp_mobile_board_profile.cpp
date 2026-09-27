#include "ktp_mobile_board_profile.h"

#include "../../core/log.h"
#include "ktp_mobile_id.h"

namespace {

constexpr KtpMobileBoardProfile kProfiles[] = {
    {BoardId::HmiKtp400FMobile,       KtpMobileOpType::Ktp400F,       {480u, 272u}, "4in",   true},
    {BoardId::HmiKtp700Mobile,        KtpMobileOpType::Ktp700,        {800u, 480u}, "7_9in", false},
    {BoardId::HmiKtp700FMobile,       KtpMobileOpType::Ktp700F,       {800u, 480u}, "7_9in", true},
    {BoardId::HmiKtp900Mobile,        KtpMobileOpType::Ktp900,        {800u, 480u}, "7_9in", false},
    {BoardId::HmiKtp900FMobile,       KtpMobileOpType::Ktp900F,       {800u, 480u}, "7_9in", true},
    {BoardId::HmiTp1000fMobile,       KtpMobileOpType::Tp1000F,       {800u, 480u}, "10in",  true},
    {BoardId::HmiKtp700FHwMobile,     KtpMobileOpType::Ktp700FHw,     {800u, 480u}, "7_9in", true},
    {BoardId::HmiKtp700FArcticMobile, KtpMobileOpType::Ktp700FArctic, {800u, 480u}, "7_9in", true},
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
