#include "ktp_mobile_hardware_info.h"

#include <iterator>

namespace {

constexpr char kHex[] = "0123456789ABCDEF";

void PushVarint(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t group[5];
    std::size_t count = 0;
    do {
        group[count++] = static_cast<uint8_t>(value & 0x7Fu);
        value >>= 7u;
    } while (value != 0u);
    while (count > 1u) out.push_back(static_cast<uint8_t>(0x80u | group[--count]));
    out.push_back(group[0]);
}

/* hmi_tp1000f_mobile_v17 bspio.dll sub_41D1B268: an attribute is its id varint, a
   big-endian 16-bit type, then type 1 one byte read as bool, type 2 one byte, type 3
   a big-endian 16-bit value (sub_41D1AE9C), type 4 an unsigned varint (sub_41D1AF44)
   and type 8 a varint whose first group is sign-extended from bit 6 (sub_41D1AF80). */
enum : uint8_t { kOmsBool = 1u, kOmsU8 = 2u, kOmsU16 = 3u, kOmsVarint = 4u, kOmsSignedVarint = 8u };

void PushAttribute(std::vector<uint8_t>& out, uint32_t id, uint8_t type, uint32_t value) {
    out.push_back(0xA3);
    PushVarint(out, id);
    out.push_back(0x00);
    out.push_back(type);
    switch (type) {
    case kOmsBool: out.push_back(value != 0u ? 1u : 0u); break;
    case kOmsU8: out.push_back(static_cast<uint8_t>(value)); break;
    case kOmsU16:
        out.push_back(static_cast<uint8_t>(value >> 8u));
        out.push_back(static_cast<uint8_t>(value));
        break;
    case kOmsVarint: PushVarint(out, value); break;
    case kOmsSignedVarint: {
        uint8_t group[6];
        std::size_t count = 0;
        do {
            group[count++] = static_cast<uint8_t>(value & 0x7Fu);
            value >>= 7u;
        } while (value != 0u);
        if (group[count - 1u] & 0x40u) group[count++] = 0u;
        while (count > 1u) out.push_back(static_cast<uint8_t>(0x80u | group[--count]));
        out.push_back(group[0]);
        break;
    }
    }
}

void PushUintProperty(std::vector<uint8_t>& out, uint8_t id_hi, uint8_t id_lo, uint32_t value) {
    out.push_back(0xA3);
    out.push_back(0x81);
    out.push_back(id_hi);
    out.push_back(id_lo);
    out.push_back(0x00);
    out.push_back(0x04);
    PushVarint(out, value);
}

/* hmi_ktp700_mobile_v17 DeviceManager.exe sub_54B68 registers the display class
   attributes with these ids and OMS::ValueType codes. */
void PushDisplayObject(std::vector<uint8_t>& out, const KtpMobilePanel& panel) {
    const uint32_t h_period = panel.width + panel.hsync_width + panel.hstart_width + panel.hend_width;
    const uint32_t v_period = panel.height + panel.vsync_width + panel.vstart_width + panel.vend_width;
    PushAttribute(out, 18265u, kOmsVarint, panel.width);
    PushAttribute(out, 18266u, kOmsVarint, panel.height);
    PushAttribute(out, 18267u, kOmsVarint, 0u);
    PushAttribute(out, 18268u, kOmsVarint, 0u);
    /* hmi_ktp700_mobile_v17 backlight.dll sub_EF2C3FA0 builds the 101-step PWM table
       as table[0] = MaxReg and, with MaxReg <= MinReg, table[b] =
       (MinReg - OffsetC) * 2^(-(100 - b) * FactorF / GammaB) + OffsetC. MaxReg and
       MinReg take BrightnessMin_PWM = 0 and BrightnessMax_PWM = 33000 from
       KTP_7_9_Mobile_V17_0.fwf default.hv; GammaB 50 and FactorF 1 are a chosen
       slope, no reference on hand has the panel's curve. */
    PushAttribute(out, 18756u, kOmsSignedVarint, 33000u);
    PushAttribute(out, 18757u, kOmsSignedVarint, 0u);
    PushAttribute(out, 18758u, kOmsSignedVarint, 50u);
    PushAttribute(out, 18759u, kOmsSignedVarint, 0u);
    PushAttribute(out, 18760u, kOmsSignedVarint, 1u);
    /* hmi_ktp700_mobile_v17 ddi_wrapper.dll sub_EF232EEC selects the LVDS1 key for
       Interface 0 and copies DataColorBits, PixelClock, the pulse widths and porches,
       and the polarities into it: HStartWidth is HsyncFrontPorch, HEndWidth
       HsyncBackPorch, VStartWidth VsyncFrontPorch, VEndWidth VsyncBackPorch,
       OuputEnablePolarity EnPolarity and ClkPol DePolarity. */
    PushAttribute(out, 18911u, kOmsU8, 0u);
    PushAttribute(out, 18912u, kOmsU8, 0u);
    PushAttribute(out, 18913u, kOmsU8, 0u);
    PushAttribute(out, 18914u, kOmsU8, panel.data_bus_width);
    PushAttribute(out, 18915u, kOmsU8, 0u);
    PushAttribute(out, 18916u, kOmsVarint, panel.pixel_clock_hz);
    PushAttribute(out, 18917u, kOmsU16, v_period);
    PushAttribute(out, 18918u, kOmsU16, panel.vsync_width);
    PushAttribute(out, 18919u, kOmsU16, panel.height);
    PushAttribute(out, 18920u, kOmsU16, panel.vend_width);
    PushAttribute(out, 18921u, kOmsU16, panel.vstart_width);
    PushAttribute(out, 18922u, kOmsBool, 0u);
    PushAttribute(out, 18923u, kOmsU16, h_period);
    PushAttribute(out, 18924u, kOmsU16, panel.hsync_width);
    PushAttribute(out, 18925u, kOmsU16, panel.width);
    PushAttribute(out, 18926u, kOmsU16, panel.hend_width);
    PushAttribute(out, 18927u, kOmsU16, panel.hstart_width);
    PushAttribute(out, 18928u, kOmsBool, 0u);
    PushAttribute(out, 18929u, kOmsBool, 0u);
    PushAttribute(out, 18930u, kOmsBool, 0u);
    /* KTP_7_9_Mobile_V17_0.fwf default.hv [HKLM\Drivers\Display\Configuration\LVDS1]
       OuputEnablePolarity = 1 in every Mobile ROM. */
    PushAttribute(out, 18931u, kOmsBool, 1u);
    for (uint32_t id = 18932u; id <= 18935u; ++id) PushAttribute(out, id, kOmsU16, 0u);
    PushAttribute(out, 18936u, kOmsVarint, 0u);
    PushAttribute(out, 18937u, kOmsU16, 0u);
    PushAttribute(out, 18938u, kOmsU16, 0u);
}

}

std::vector<uint8_t> BuildKtpMobileHardwareInfoOms(const std::array<uint8_t, 6>& mac, KtpMobileOpType op_type,
                                                   const KtpMobilePanel& panel) {
    /* hmi_ktp400_mobile_v13 bspio.dll HWI_GetMACAddressInfo @0x418851DC reads
       MicroOMS object 0x474C, child 0x4928, and string property 0x492B. */

    static constexpr uint8_t kRootObject[] = {0x03,
                                              /* hmi_ktp400_mobile_v17 DeviceManager.exe @0x17E80 and @0x72DC8. */
                                              0xA1, 0x01, 0x00, 0x00, 0x01, 0x81, 0x8E, 0x2F, 0x20, 0x00};

    /* hmi_ktp700_mobile_v17 backlight.dll sub_EF2C4038 and bspio.dll
       HWI_GetDisplayAttributes @0x41D15730 reach the display attributes through root
       child 18239, then 18264. */
    static constexpr uint8_t kDisplayObject[] = {0xA1, 0x01, 0x00, 0x00, 0x02, 0x81, 0x8E, 0x3F, 0x20, 0x00,
                                                 0xA1, 0x01, 0x00, 0x00, 0x03, 0x81, 0x8E, 0x58, 0x20, 0x00};

    /* hmi_ktp400_mobile_v13 bspio.dll HWI_GetOPTypeEx @0x418852B8. */
    static constexpr uint8_t kOpTypeObject[] = {0xA1, 0x01, 0x00, 0x00, 0x06, 0x81, 0x92, 0x07, 0x20,
                                                0x00, 0xA3, 0x81, 0x92, 0x0A, 0x00, 0x04, 0x40};

    static constexpr uint8_t kMacObjectPrefix[] = {0xA1, 0x01, 0x00, 0x00, 0x07, 0x81, 0x8E, 0x4C, 0x20,
                                                   0x00, 0xA1, 0x01, 0x00, 0x00, 0x08, 0x81, 0x92, 0x28,
                                                   0x20, 0x00, 0xA3, 0x81, 0x92, 0x2B, 0x00, 0x15, 0x11};

    std::vector<uint8_t> oms(std::begin(kRootObject), std::end(kRootObject));
    oms.insert(oms.end(), std::begin(kDisplayObject), std::end(kDisplayObject));
    PushDisplayObject(oms, panel);
    oms.push_back(0xA2);
    oms.push_back(0xA2);
    oms.insert(oms.end(), std::begin(kOpTypeObject), std::end(kOpTypeObject));
    PushUintProperty(oms, 0x92, 0x0D, static_cast<uint32_t>(op_type));
    oms.push_back(0xA2);

    oms.insert(oms.end(), std::begin(kMacObjectPrefix), std::end(kMacObjectPrefix));
    for (std::size_t i = 0; i < mac.size(); ++i) {
        if (i != 0u) oms.push_back(':');
        oms.push_back(static_cast<uint8_t>(kHex[mac[i] >> 4u]));
        oms.push_back(static_cast<uint8_t>(kHex[mac[i] & 0x0Fu]));
    }
    oms.push_back(0xA2);
    oms.push_back(0xA2);
    oms.push_back(0xA2);
    return oms;
}

