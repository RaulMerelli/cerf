#include "ktp_mobile_board_profile.h"

#include "../../core/log.h"

namespace {

constexpr KtpMobileBoardProfile kProfiles[] = {
    {Board::HmiKtp400FMobile,       KtpMobileOpType::Ktp400F,       {480u, 272u}, "4in",   true},
    {Board::HmiKtp700Mobile,        KtpMobileOpType::Ktp700,        {800u, 480u}, "7_9in", false},
    {Board::HmiKtp700FMobile,       KtpMobileOpType::Ktp700F,       {800u, 480u}, "7_9in", true},
    {Board::HmiKtp900Mobile,        KtpMobileOpType::Ktp900,        {800u, 480u}, "7_9in", false},
    {Board::HmiKtp900FMobile,       KtpMobileOpType::Ktp900F,       {800u, 480u}, "7_9in", true},
    {Board::HmiTp1000fMobile,       KtpMobileOpType::Tp1000F,       {800u, 480u}, "10in",  true},
    {Board::HmiKtp700FHwMobile,     KtpMobileOpType::Ktp700FHw,     {800u, 480u}, "7_9in", true},
    {Board::HmiKtp700FArcticMobile, KtpMobileOpType::Ktp700FArctic, {800u, 480u}, "7_9in", true},
};

}

const KtpMobileBoardProfile* TryKtpMobileBoardProfileFor(Board board) {
    for (const auto& profile : kProfiles)
        if (profile.board == board) return &profile;
    return nullptr;
}

const KtpMobileBoardProfile& KtpMobileBoardProfileFor(Board board) {
    if (const auto* profile = TryKtpMobileBoardProfileFor(board)) return *profile;
    LOG(Caution, "KtpMobileBoardProfile: unsupported board %u\n", static_cast<unsigned>(board));
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}
