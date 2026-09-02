#include "ktp_mobile_hardware_info.h"

#include <iterator>

namespace {

constexpr char kHex[] = "0123456789ABCDEF";

/* 7-bit big-endian varint, the encoding the MicroOMS stream uses for uint
   properties: continuation in bit 7, most significant group first. */
void PushVarint(std::vector<uint8_t>& out, uint32_t value) {
    if (value >= 0x80u) out.push_back(static_cast<uint8_t>(0x80u | ((value >> 7u) & 0x7Fu)));
    out.push_back(static_cast<uint8_t>(value & 0x7Fu));
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

} // namespace

namespace {

std::vector<uint8_t> BuildHardwareInfo(const std::array<uint8_t, 6>& mac, KtpMobileOpType op_type,
                                       const KtpMobilePanel* panel) {
    /* The base object is the MicroOMS hardware-info tree extracted from the
       Siemens KTP400 V13 FWF.  The final root-object A2 is appended below.

       IDA hmi_ktp400_mobile_v13 bspio.dll HWI_GetMACAddressInfo 0x418851DC walks object 0x474C,
       child 0x4928 and string property 0x492B.  Its parser at 0x41884930
       requires exactly 17 characters and accepts ':' separators.  The FWF
       MicroOMS stream encodes an object as A1/rid/class/20/00 ... A2 and an
       ASCII string property as A3/id/00/15/length/payload. */

    /* Up to and including the header of object 0x8E58, whose first two
       properties are the panel width and height. */
    static constexpr uint8_t kRootObject[] = {0x03,
                                              /* DeviceManager.exe 0x17E80 creates the Device object (0x4728).
                                                 LoadHardwareDescription at 0x72DC8 removes/recreates its composed
                                                 HardwareDescription object, class 0x472F, from this stream. */
                                              0xA1, 0x01, 0x00, 0x00, 0x01, 0x81, 0x8E, 0x2F, 0x20, 0x00};

    static constexpr uint8_t kPanelObject[] = {0xA1, 0x01, 0x00, 0x00, 0x02, 0x81, 0x8E, 0x3F, 0x20, 0x00,
                                               0xA1, 0x01, 0x00, 0x00, 0x03, 0x81, 0x8E, 0x58, 0x20, 0x00};

    /* The remaining properties of object 0x8E58, its siblings, and the header
       of object 0x9207 with its first field.  bspio's device selector compares
       the pair returned by HWI_GetOPTypeEx - property 0x920A followed by
       property 0x920D - against its { 0x40, bit } table. */
    static constexpr uint8_t kPanelRest[] = {
        0xA3, 0x81, 0x8E, 0x5B, 0x00, 0x04, 0x80, 0x5F, 0xA3, 0x81, 0x8E, 0x5C, 0x00, 0x04, 0x80, 0x36,
        0xA3, 0x81, 0x92, 0x44, 0x00, 0x04, 0x01, 0xA3, 0x81, 0x92, 0x45, 0x00, 0x04, 0x0F, 0xA3, 0x81,
        0x92, 0x46, 0x00, 0x04, 0x80, 0x64, 0xA3, 0x81, 0x92, 0x47, 0x00, 0x04, 0x00, 0xA3, 0x81, 0x92,
        0x48, 0x00, 0x04, 0x80, 0x64, 0xA2, 0xA1, 0x01, 0x00, 0x00, 0x04, 0x81, 0x8E, 0x5E, 0x20, 0x00,
        0xA2, 0xA1, 0x01, 0x00, 0x00, 0x05, 0x81, 0x8E, 0x63, 0x20, 0x00, 0xA2, 0xA2};

    static constexpr uint8_t kOpTypeObject[] = {0xA1, 0x01, 0x00, 0x00, 0x06, 0x81, 0x92, 0x07, 0x20,
                                                0x00, 0xA3, 0x81, 0x92, 0x0A, 0x00, 0x04, 0x40};

    static constexpr uint8_t kMacObjectPrefix[] = {0xA1, 0x01, 0x00, 0x00, 0x07, 0x81, 0x8E, 0x4C, 0x20,
                                                   0x00, 0xA1, 0x01, 0x00, 0x00, 0x08, 0x81, 0x92, 0x28,
                                                   0x20, 0x00, 0xA3, 0x81, 0x92, 0x2B, 0x00, 0x15, 0x11};

    std::vector<uint8_t> oms(std::begin(kRootObject), std::end(kRootObject));
    if (panel) {
        oms.insert(oms.end(), std::begin(kPanelObject), std::end(kPanelObject));
        PushUintProperty(oms, 0x8E, 0x59, panel->width);
        PushUintProperty(oms, 0x8E, 0x5A, panel->height);
        oms.insert(oms.end(), std::begin(kPanelRest), std::end(kPanelRest));
    }
    oms.insert(oms.end(), std::begin(kOpTypeObject), std::end(kOpTypeObject));
    PushUintProperty(oms, 0x92, 0x0D, static_cast<uint32_t>(op_type));
    oms.push_back(0xA2); // 0x9207

    oms.insert(oms.end(), std::begin(kMacObjectPrefix), std::end(kMacObjectPrefix));
    for (std::size_t i = 0; i < mac.size(); ++i) {
        if (i != 0u) oms.push_back(':');
        oms.push_back(static_cast<uint8_t>(kHex[mac[i] >> 4u]));
        oms.push_back(static_cast<uint8_t>(kHex[mac[i] & 0x0Fu]));
    }
    oms.push_back(0xA2); // 0x4928
    oms.push_back(0xA2); // 0x474C
    oms.push_back(0xA2); // root
    return oms;
}

} // namespace

std::vector<uint8_t> BuildKtpMobileHardwareInfoOms(const std::array<uint8_t, 6>& mac, KtpMobileOpType op_type,
                                                   KtpMobilePanel panel) {
    return BuildHardwareInfo(mac, op_type, &panel);
}

std::vector<uint8_t> BuildKtpMobileInstalledHardwareDescriptionOms(const std::array<uint8_t, 6>& mac,
                                                                   KtpMobileOpType op_type) {
    return BuildHardwareInfo(mac, op_type, nullptr);
}
