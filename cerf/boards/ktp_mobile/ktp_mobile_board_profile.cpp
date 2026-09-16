#include "ktp_mobile_board_profile.h"

#include "../../core/log.h"

namespace {

constexpr KtpMobileBoardProfile kProfiles[] = {
    {Board::HmiKtp400FMobile,       KtpMobileOpType::Ktp400F,       {480u, 272u}, "4in"},
    {Board::HmiKtp700Mobile,        KtpMobileOpType::Ktp700,        {800u, 480u}, "7_9in"},
    {Board::HmiKtp700FMobile,       KtpMobileOpType::Ktp700F,       {800u, 480u}, "7_9in"},
    {Board::HmiKtp900Mobile,        KtpMobileOpType::Ktp900,        {800u, 480u}, "7_9in"},
    {Board::HmiKtp900FMobile,       KtpMobileOpType::Ktp900F,       {800u, 480u}, "7_9in"},
    {Board::HmiTp1000fMobile,       KtpMobileOpType::Tp1000F,       {800u, 480u}, "10in"},
    {Board::HmiKtp700FHwMobile,     KtpMobileOpType::Ktp700FHw,     {800u, 480u}, "7_9in"},
    {Board::HmiKtp700FArcticMobile, KtpMobileOpType::Ktp700FArctic, {800u, 480u}, "7_9in"},
};

}

const KtpMobileBoardProfile& KtpMobileBoardProfileFor(Board board) {
    for (const auto& profile : kProfiles)
        if (profile.board == board) return profile;
    LOG(Caution, "KtpMobileBoardProfile: unsupported board %u\n", static_cast<unsigned>(board));
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}
