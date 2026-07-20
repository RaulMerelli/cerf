#include "../../socs/imx51/usb_device_host.h"

#include "../../boot/sec_flash.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../core/service.h"
#include "../../boards/board_context.h"
#include "../../host/host_icon_cache.h"
#include "../../host/host_widget.h"
#include "../../host/host_widget_registry.h"
#include "../../socs/imx51/imx51_usboh3.h"
#include "../../state/state_stream.h"

#include <cstdint>
#include <cstring>

namespace {

uint32_t Rd32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void Wr32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);       p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16); p[3] = static_cast<uint8_t>(v >> 24);
}

/* Ford SYNC2 USB host driver (CERF plays the host PC for SBOOT's USB-device
   flasher). Control-request encodings grounded in SBOOT sub_8005F0A4 /
   sub_8005EAFC / sub_8005EE80 / sub_8005EECC; the post-enumeration download
   protocol in sub_8004FF60. */
class FordSync2UsbFlasher : public Service, public UsbDeviceHost, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::FordSyncGen2;
    }

    void OnReady() override {
        emu_.Get<Imx51Usboh3>().RegisterDeviceHost(this);
        emu_.Get<HostWidgetRegistry>().Register(this);
    }

    void OnDeviceReset() override {
        LOG(UsbOtg, "[USBHOST] device reset -> enumerate\n");
        step_ = Step::kDevDesc;
        dl_   = DlPhase::kNone;
        GetDescriptor(kDescDevice, 18);
    }

    void OnDeviceIn(uint32_t ep, const uint8_t* data, uint32_t len) override {
        if (ep != 0) { OnBulkIn(data, len); return; }
        /* EP0 IN: a non-zero length is a control DATA stage (advance on its status
           stage); a zero length is the status stage of a no-data request. */
        if (len == 0) AdvanceAfterStatus();
        else          CaptureData(data, len);
    }

    uint32_t OnDeviceOut(uint32_t ep, uint8_t* dst, uint32_t max) override {
        if (ep == 0) { AdvanceAfterStatus(); return 0; }  /* control status stage */
        return ServeDownload(dst, max);
    }

    void SaveWidgetState(StateWriter& w) const override {
        w.Write<uint8_t>(static_cast<uint8_t>(step_));
        w.Write(conf_total_);
        w.Write<uint8_t>(static_cast<uint8_t>(dl_));
        w.Write(cat_off_);
        w.Write(cat_remaining_);
        w.Write(seg_off_);
        w.Write(seg_remaining_);
    }
    void RestoreWidgetState(StateReader& r) override {
        uint8_t s = 0; r.Read(s); step_ = static_cast<Step>(s);
        r.Read(conf_total_);
        uint8_t d = 0; r.Read(d); dl_ = static_cast<DlPhase>(d);
        r.Read(cat_off_);
        r.Read(cat_remaining_);
        r.Read(seg_off_);
        r.Read(seg_remaining_);
    }

    // Once we have USB somewhere, this should become hot-pluggable device follwing PCMCIA example
    std::wstring WidgetName() const override { return L"USB"; }
    WidgetGroup  Group() const override { return WidgetGroup::Usb; }
    std::wstring Tooltip() const override { return L"USB - [Ford Sync 2] Factory Flashing Tool"; }
    void DrawIcon(HDC dc, const RECT& box) const override {
        emu_.Get<HostIconCache>().DrawCentered(dc, box, L"ICON_USB_FORD_FLASHER");
    }

private:
    enum class Step { kIdle, kDevDesc, kSetAddr, kConfShort, kConfFull, kSetConfig, kConfigured };

    /* Ford download protocol phases (sub_8004FF60). Increment 1 answers the 124B
       request with the 60B TransactionInfo; the image-header/CAT/segment phases
       follow. */
    enum class DlPhase { kNone, kTxnInfo, kImgHdr, kCat, kSegment, kDone };

    static constexpr uint8_t  kDescDevice   = 0x01;
    static constexpr uint8_t  kDescConfig   = 0x02;
    static constexpr uint8_t  kDescEndpoint = 0x05;
    static constexpr uint32_t kFordSig      = 0xCB00C001u;

    void Send(uint8_t bm_req, uint8_t b_req, uint16_t w_value,
              uint16_t w_index, uint16_t w_length) {
        const uint8_t s[8] = {
            bm_req, b_req,
            static_cast<uint8_t>(w_value),  static_cast<uint8_t>(w_value >> 8),
            static_cast<uint8_t>(w_index),  static_cast<uint8_t>(w_index >> 8),
            static_cast<uint8_t>(w_length), static_cast<uint8_t>(w_length >> 8),
        };
        emu_.Get<Imx51Usboh3>().DeliverSetup(s);
    }
    /* wValue = (type << 8) | index 0 (GET_DESCRIPTOR, bmRequestType IN/std/device). */
    void GetDescriptor(uint8_t type, uint16_t len) {
        Send(0x80, 0x06, static_cast<uint16_t>(type << 8), 0, len);
    }
    void SetAddress(uint16_t addr) { Send(0x00, 0x05, addr, 0, 0); }       /* sub_8005EE80 */
    void SetConfiguration(uint16_t cfg) { Send(0x00, 0x09, cfg, 0, 0); }   /* sub_8005EECC */

    void CaptureData(const uint8_t* d, uint32_t len) {
        switch (step_) {
            case Step::kDevDesc:
                if (len >= 12)
                    LOG(UsbOtg, "[USBHOST] device descriptor bLength=%u VID=0x%04X PID=0x%04X\n",
                        d[0], d[8] | (d[9] << 8), d[10] | (d[11] << 8));
                break;
            case Step::kConfShort:
                if (len >= 4) conf_total_ = static_cast<uint16_t>(d[2] | (d[3] << 8));
                LOG(UsbOtg, "[USBHOST] config descriptor wTotalLength=%u\n", conf_total_);
                break;
            case Step::kConfFull:
                LogEndpoints(d, len);
                break;
            default:
                break;
        }
    }

    /* Walk the config-descriptor chain (each: bLength@0, bDescriptorType@1) and
       log the endpoint descriptors (bEndpointAddress@2, bmAttributes@3) the Ford
       bulk transport uses. */
    void LogEndpoints(const uint8_t* d, uint32_t len) {
        for (uint32_t i = 0; i + 2 <= len;) {
            const uint8_t blen = d[i], btype = d[i + 1];
            if (blen == 0) break;
            if (btype == kDescEndpoint && i + 4 <= len)
                LOG(UsbOtg, "[USBHOST] endpoint addr=0x%02X attr=0x%02X\n", d[i + 2], d[i + 3]);
            i += blen;
        }
    }

    void AdvanceAfterStatus() {
        switch (step_) {
            case Step::kDevDesc:   step_ = Step::kSetAddr;   SetAddress(1);                  break;
            case Step::kSetAddr:   step_ = Step::kConfShort; GetDescriptor(kDescConfig, 9);  break;
            case Step::kConfShort: step_ = Step::kConfFull;  GetDescriptor(kDescConfig, conf_total_); break;
            case Step::kConfFull:  step_ = Step::kSetConfig; SetConfiguration(1);            break;
            case Step::kSetConfig: step_ = Step::kConfigured;
                LOG(UsbOtg, "[USBHOST] device CONFIGURED - enumeration complete\n");           break;
            default: break;
        }
    }

    /* Device->host bulk IN: the 124-byte download request (sig 0xCB00C001) opens
       the download (sub_8004FF60); later 28-byte responses are status acks. */
    void OnBulkIn(const uint8_t* data, uint32_t len) {
        const uint32_t sig = (len >= 4) ? Rd32(data) : 0u;
        if (dl_ == DlPhase::kNone && sig == kFordSig) {
            LOG(UsbOtg, "[USBHOST] Ford download request (%u B) -> sending TransactionInfo\n", len);
            dl_ = DlPhase::kTxnInfo;
        }
    }

    /* Host->device bulk OUT: feed the Ford download protocol. */
    uint32_t ServeDownload(uint8_t* dst, uint32_t max) {
        uint32_t got = 0;
        switch (dl_) {
            case DlPhase::kTxnInfo: got = SendTxnInfo(dst, max); break;
            case DlPhase::kImgHdr:  got = SendImgHdr(dst, max);  break;
            case DlPhase::kCat:     got = SendCat(dst, max);     break;
            case DlPhase::kSegment: got = SendSeg(dst, max);     break;
            default:                return 0;   /* download complete */
        }
        if (got) MarkTx();
        return got;
    }

    /* 60-byte TransactionInfo (sub_8004FF60: gate sig + size==60; type@+0x18=0 =
       download+flash). SegSize@+0x10 and ImageSize@+0xC are read from the `.sec`
       image-header prefix (SegSize = chunk_stride@+0x28, ImageSize = SEC size@+0x18);
       SegPerTR@+0x14=1 -> one chunk per transfer = chunk_count downloads. */
    uint32_t SendTxnInfo(uint8_t* dst, uint32_t max) {
        if (max < 60) return 0;
        uint8_t hdr[0x2C] = {0};
        emu_.Get<SecFlash>().ReadRaw(0, hdr, sizeof(hdr));
        std::memset(dst, 0, 60);
        Wr32(dst + 0x00, kFordSig);
        Wr32(dst + 0x04, 60);
        Wr32(dst + 0x0C, Rd32(hdr + 0x18));   /* ImageSize = SEC image size */
        Wr32(dst + 0x10, Rd32(hdr + 0x28));   /* SegSize   = .sec chunk stride */
        Wr32(dst + 0x14, 1);                  /* SegPerTR */
        Wr32(dst + 0x18, 0);                  /* transaction type 0 = download+flash */
        dl_ = DlPhase::kImgHdr;
        LOG(UsbOtg, "[USBHOST] sent TransactionInfo (download+flash, SegSize=0x%X ImageSize=0x%X)\n",
            Rd32(hdr + 0x28), Rd32(hdr + 0x18));
        return 60;
    }

    /* 128-byte Ford image header = .sec[0:0x80] verbatim (sub_8004FF60 copies it to
       the cache + gates ImageType@+8). Capture the CAT extent (start = HdrSize@+0xC,
       len = CAT-recv@+0x20) for the stream that follows. */
    uint32_t SendImgHdr(uint8_t* dst, uint32_t max) {
        if (max < 0x80) return 0;
        uint8_t hdr[0x80] = {0};
        emu_.Get<SecFlash>().ReadRaw(0, hdr, sizeof(hdr));
        std::memcpy(dst, hdr, 0x80);
        cat_off_       = Rd32(hdr + 0x0C);   /* HdrSize -> CAT start (0x80) */
        cat_remaining_ = Rd32(hdr + 0x20);   /* CAT receive length */
        seg_remaining_ = Rd32(hdr + 0x14);   /* SGMSize = payload bytes (= 251 segments) */
        dl_ = DlPhase::kCat;
        LOG(UsbOtg, "[USBHOST] sent image header (ImageType=0x%X CAT@0x%llX len=0x%X)\n",
            Rd32(hdr + 0x08), static_cast<unsigned long long>(cat_off_), cat_remaining_);
        return 0x80;
    }

    /* PKCS#7 CAT blob, streamed from .sec[cat_off_..] in the device's ≤0x1FFF recv
       chunks (sub_8005CF00) until exhausted, then the segment phase. */
    uint32_t SendCat(uint8_t* dst, uint32_t max) {
        if (cat_remaining_ == 0) { dl_ = DlPhase::kSegment; return 0; }
        const uint32_t n   = (max < cat_remaining_) ? max : cat_remaining_;
        const uint32_t got = static_cast<uint32_t>(emu_.Get<SecFlash>().ReadRaw(cat_off_, dst, n));
        cat_off_       += got;
        cat_remaining_ -= got;
        if (cat_remaining_ == 0) {
            seg_off_ = cat_off_;   /* CAT ends exactly at payload_off */
            dl_ = DlPhase::kSegment;
            LOG(UsbOtg, "[USBHOST] CAT sent -> segment phase (payload @0x%llX len=0x%X)\n",
                static_cast<unsigned long long>(seg_off_), seg_remaining_);
        }
        return got;
    }

    /* Image payload = .sec[payload_off : EOF] (SGMSize bytes = the 251 signed
       segments), streamed in the device's ≤0x1FFF recv chunks. The flasher verifies
       each (software MinCrypt) then programs it into the NAND via the NFC. */
    uint32_t SendSeg(uint8_t* dst, uint32_t max) {
        if (seg_remaining_ == 0) { dl_ = DlPhase::kDone; return 0; }
        const uint32_t n   = (max < seg_remaining_) ? max : seg_remaining_;
        const uint32_t got = static_cast<uint32_t>(emu_.Get<SecFlash>().ReadRaw(seg_off_, dst, n));
        seg_off_       += got;
        seg_remaining_ -= got;
        if (seg_remaining_ == 0) { dl_ = DlPhase::kDone; LOG(UsbOtg, "[USBHOST] payload sent\n"); }
        return got;
    }

    Step     step_          = Step::kIdle;
    uint16_t conf_total_    = 0;
    DlPhase  dl_            = DlPhase::kNone;
    uint64_t cat_off_       = 0;
    uint32_t cat_remaining_ = 0;
    uint64_t seg_off_       = 0;
    uint32_t seg_remaining_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(FordSync2UsbFlasher);
