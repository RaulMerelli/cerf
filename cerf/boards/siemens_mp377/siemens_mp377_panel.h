#pragma once

#include <cstdint>

namespace siemens_mp377 {

/* Single source of truth for the MP377 panel profile exported through the
   P377 HWI OPTypeEx block and consumed by the MP377 display/touch models.

   bspio.dll's MP377 name table accepts family 4 variants only:
     variant 1 = MP377_12Key, variant 2 = MP377_12Touch,
     variant 4 = MP377_15Touch, variant 8 = MP377_19Touch.
   nk.exe's LCD-size path maps family 4 / variant 2 to 800x600x16. */
enum class SiemensMp377PanelProfile {
    Inch12_800x600,
    Inch15_1024x768,
    Inch19_1280x1024,
};

struct SiemensMp377PanelDescriptor {
    SiemensMp377PanelProfile profile;
    uint8_t op_type_family;
    uint8_t op_type_variant;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

inline constexpr SiemensMp377PanelDescriptor Mp377PanelDescriptor(SiemensMp377PanelProfile p) {
    return p == SiemensMp377PanelProfile::Inch12_800x600
        ? SiemensMp377PanelDescriptor{ p, 4u, 2u,  800u,  600u, 16u }
        : p == SiemensMp377PanelProfile::Inch15_1024x768
        ? SiemensMp377PanelDescriptor{ p, 4u, 4u, 1024u,  768u, 16u }
        : SiemensMp377PanelDescriptor{ p, 4u, 8u, 1280u, 1024u, 16u };
}

/* CERF currently models the commercial 12" MP377 Touch profile.  Changing this
   profile updates the HWI OPTypeEx bytes, SM501 framebuffer dimensions, renderer
   fallback pitch and touch calibration selection together. */
inline constexpr SiemensMp377PanelProfile kMp377HwiPanelProfile =
    SiemensMp377PanelProfile::Inch12_800x600;
inline constexpr SiemensMp377PanelDescriptor kMp377HwiPanel =
    Mp377PanelDescriptor(kMp377HwiPanelProfile);

inline constexpr uint32_t Mp377PanelWidth(SiemensMp377PanelProfile p) {
    return Mp377PanelDescriptor(p).width;
}

inline constexpr uint32_t Mp377PanelHeight(SiemensMp377PanelProfile p) {
    return Mp377PanelDescriptor(p).height;
}

} // namespace siemens_mp377

