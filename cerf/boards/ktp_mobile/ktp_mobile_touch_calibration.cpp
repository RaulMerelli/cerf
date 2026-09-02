#include "ktp_mobile_touch_calibration.h"

#include "../../boot/rom_parser_service.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

/* HKLM\HARDWARE\DEVICEMAP\TOUCH\CalibrationData, from the registry hive each
   ROM ships.  The hive is stored compressed inside the XIP.

     V13 hive, single value `CalibrationData`
       KTP400   2004,2034 853,3046 868,946 3149,977 3155,3054
       KTP700   1996,2063 853,3170 849,951 3136,962 3137,3152
     V14 through V17 hives, one value per diagonal and identical across
     those four releases
       4in      2004,2034 853,3046 868,946 3149,977 3155,3054
       7_9in    1981,2051 830,3146 836,913 3143,942 3122,3157
       10in     2039,2015 869,3179 865,913 3179,921 3187,3147 */
struct RawPoints {
    uint16_t x[5];
    uint16_t y[5];
};

constexpr RawPoints kCal4in = {{2004, 853, 868, 3149, 3155}, {2034, 3046, 946, 977, 3054}};
constexpr RawPoints kCal79inV13 = {{1996, 853, 849, 3136, 3137}, {2063, 3170, 951, 962, 3152}};
constexpr RawPoints kCal79inSuffixed = {{1981, 830, 836, 3143, 3122}, {2051, 3146, 913, 942, 3157}};
constexpr RawPoints kCal10inSuffixed = {{2039, 869, 865, 3179, 3187}, {2015, 3179, 913, 921, 3147}};

struct Point {
    double x, y;
};

/* tchproxy!TouchPanelGetDeviceCaps hands the calibration UI five crosshairs:
   the centre first, then the W/5,H/5 insets in the order upper-left,
   lower-left, lower-right, upper-right. */
void Crosshairs(uint32_t w, uint32_t h, Point out[5]) {
    const double dx = static_cast<double>(w / 5u);
    const double dy = static_cast<double>(h / 5u);
    out[0] = {static_cast<double>(w / 2u), static_cast<double>(h / 2u)};
    out[1] = {dx, dy};
    out[2] = {dx, static_cast<double>(h) - dy};
    out[3] = {static_cast<double>(w) - dx, static_cast<double>(h) - dy};
    out[4] = {static_cast<double>(w) - dx, dy};
}

/* Least squares for raw = a*sx + b*sy + c, through the 3x3 normal equations
   with partial pivoting. */
bool Solve(const Point screen[5], const double raw[5], double out[3]) {
    double m[3][4] = {};
    for (uint32_t i = 0; i < 5u; ++i) {
        const double v[3] = {screen[i].x, screen[i].y, 1.0};
        for (uint32_t r = 0; r < 3u; ++r) {
            for (uint32_t c = 0; c < 3u; ++c)
                m[r][c] += v[r] * v[c];
            m[r][3] += v[r] * raw[i];
        }
    }
    for (uint32_t i = 0; i < 3u; ++i) {
        uint32_t piv = i;
        for (uint32_t r = i + 1u; r < 3u; ++r)
            if ((m[r][i] < 0 ? -m[r][i] : m[r][i]) > (m[piv][i] < 0 ? -m[piv][i] : m[piv][i])) piv = r;
        if ((m[piv][i] < 0 ? -m[piv][i] : m[piv][i]) < 1e-9) return false;
        for (uint32_t c = 0; c < 4u; ++c) {
            const double t = m[i][c];
            m[i][c] = m[piv][c];
            m[piv][c] = t;
        }
        for (uint32_t r = i + 1u; r < 3u; ++r) {
            const double f = m[r][i] / m[i][i];
            for (uint32_t c = i; c < 4u; ++c)
                m[r][c] -= f * m[i][c];
        }
    }
    for (int i = 2; i >= 0; --i) {
        double s = m[i][3];
        for (uint32_t c = static_cast<uint32_t>(i) + 1u; c < 3u; ++c)
            s -= m[i][c] * out[c];
        out[i] = s / m[i][i];
    }
    return true;
}

/* From V14 on the touch driver names its calibration value per diagonal, so
   that name appears in the XIP; the V13 driver uses the bare name. */
bool RomHasSuffixedNames(const uint8_t* rom, size_t size) {
    static const char kMark[] = "CalibrationData7_9in";
    std::vector<uint8_t> needle;
    for (const char* p = kMark; *p; ++p) {
        needle.push_back(static_cast<uint8_t>(*p));
        needle.push_back(0u);
    }
    for (size_t at = 0; at + needle.size() <= size; ++at)
        if (std::memcmp(rom + at, needle.data(), needle.size()) == 0) return true;
    return false;
}

} /* namespace */

KtpMobileTouchMap KtpMobileTouchCalibration::Read(const char* size_suffix, uint32_t width, uint32_t height) {
    KtpMobileTouchMap map;
    if (width < 5u || height < 5u) return map;

    bool suffixed = false;
    if (auto* parser = emu_.TryGet<RomParserService>()) {
        if (!parser->Loaded().empty()) {
            const auto& flat = parser->Primary().flat;
            suffixed = !flat.empty() && RomHasSuffixedNames(flat.data(), flat.size());
        }
    }

    const RawPoints* pts = &kCal4in;
    if (std::strcmp(size_suffix, "7_9in") == 0)
        pts = suffixed ? &kCal79inSuffixed : &kCal79inV13;
    else if (std::strcmp(size_suffix, "10in") == 0)
        pts = &kCal10inSuffixed;

    Point screen[5];
    Crosshairs(width, height, screen);
    double raw_x[5], raw_y[5];
    for (uint32_t i = 0; i < 5u; ++i) {
        raw_x[i] = pts->x[i];
        raw_y[i] = pts->y[i];
    }
    double sx[3] = {}, sy[3] = {};
    if (!Solve(screen, raw_x, sx) || !Solve(screen, raw_y, sy)) return map;

    map = {true, sx[0], sx[1], sx[2], sy[0], sy[1], sy[2]};
    LOG(Boot, "KtpMobileTouch: %ux%u panel, %s hive calibration for %s\n", width, height,
        suffixed ? "per-diagonal" : "single-value", size_suffix);
    return map;
}

REGISTER_SERVICE(KtpMobileTouchCalibration);
