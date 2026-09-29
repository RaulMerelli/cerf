#pragma once

#include <string_view>

/* hmi_tp1000f_mobile_v17 bspio.dll serves every variant: HWI_IsMobile @0x41D17DB0 and
   HWI_GetOPType @0x41D15254 return constants, and HWI_IsSafety @0x41D15B90 tells the
   variants apart only by the op-type attributes HWI_GetOPTypeEx @0x41D15478 reads from
   hardware-description object 18695. */
namespace BoardId {

inline constexpr std::string_view HmiKtp400FMobile = "hmi_ktp400f_mobile";
inline constexpr std::string_view HmiKtp700Mobile = "hmi_ktp700_mobile";
inline constexpr std::string_view HmiKtp700FMobile = "hmi_ktp700f_mobile";
inline constexpr std::string_view HmiKtp900Mobile = "hmi_ktp900_mobile";
inline constexpr std::string_view HmiKtp900FMobile = "hmi_ktp900f_mobile";
inline constexpr std::string_view HmiTp1000fMobile = "hmi_tp1000f_mobile";
inline constexpr std::string_view HmiKtp700FHwMobile = "hmi_ktp700f_hw_mobile";
inline constexpr std::string_view HmiKtp700FArcticMobile = "hmi_ktp700f_arctic_mobile";


inline bool IsKtpMobile(std::string_view id) {
    return id == HmiKtp400FMobile || id == HmiKtp700Mobile || id == HmiKtp700FMobile ||
           id == HmiKtp900Mobile || id == HmiKtp900FMobile || id == HmiTp1000fMobile ||
           id == HmiKtp700FHwMobile || id == HmiKtp700FArcticMobile;
}

}
