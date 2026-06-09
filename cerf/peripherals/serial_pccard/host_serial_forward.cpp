#include "host_serial_forward.h"

#include "../../core/log.h"

namespace {
constexpr DWORD kReadTimeoutMs  = 50;     /* reader wakes to poll status + stop */
constexpr DWORD kWriteTimeoutMs = 2000;   /* a wedged device can't hang shutdown */
}  /* namespace */

HostSerialForward::HostSerialForward(std::wstring host_port)
    : port_name_(std::move(host_port)) {}

HostSerialForward::~HostSerialForward() { OnClose(); }

void HostSerialForward::OnOpen() {
    /* "\\.\" prefix is required for COM10+ and harmless for COM1..9. */
    const std::wstring path = L"\\\\.\\" + port_name_;
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                          OPEN_EXISTING, 0, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        LOG(Caution, "[SerialFwd] cannot open host %ls (err %lu); port stays idle\n",
            port_name_.c_str(), GetLastError());
        if (uart_) uart_->SetModemInputs(false, false, false, false);
        return;
    }

    ApplyLineConfig(uart_ ? uart_->GetLineConfig() : Serial16550::LineConfig{});

    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout        = MAXDWORD;   /* return on first byte, or after */
    to.ReadTotalTimeoutMultiplier = MAXDWORD;   /* ReadTotalTimeoutConstant ms if  */
    to.ReadTotalTimeoutConstant   = kReadTimeoutMs;          /* the buffer is empty */
    to.WriteTotalTimeoutConstant  = kWriteTimeoutMs;
    SetCommTimeouts(handle_, &to);

    if (uart_)
        uart_->SetLineConfigCallback(
            [this](const Serial16550::LineConfig& c) { ApplyLineConfig(c); });

    running_ = true;
    reader_  = std::thread([this] { ReaderLoop(); });
    writer_  = std::thread([this] { WriterLoop(); });
    LOG(Periph, "[SerialFwd] bridging guest COM <-> host %ls\n", port_name_.c_str());
}

void HostSerialForward::OnClose() {
    if (uart_) uart_->SetLineConfigCallback(nullptr);
    StopThreads();
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

void HostSerialForward::StopThreads() {
    running_.store(false);
    tx_cv_.notify_all();
    if (writer_.joinable()) writer_.join();
    if (reader_.joinable()) reader_.join();   /* ReadFile returns within the timeout */
}

void HostSerialForward::ApplyLineConfig(const Serial16550::LineConfig& c) {
    if (handle_ == INVALID_HANDLE_VALUE) return;
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle_, &dcb)) return;

    dcb.BaudRate = c.baud;
    dcb.ByteSize = c.data_bits;
    switch (c.parity) {
        case Serial16550::LineConfig::Parity::None:  dcb.Parity = NOPARITY;    break;
        case Serial16550::LineConfig::Parity::Odd:   dcb.Parity = ODDPARITY;   break;
        case Serial16550::LineConfig::Parity::Even:  dcb.Parity = EVENPARITY;  break;
        case Serial16550::LineConfig::Parity::Mark:  dcb.Parity = MARKPARITY;  break;
        case Serial16550::LineConfig::Parity::Space: dcb.Parity = SPACEPARITY; break;
    }
    switch (c.stop) {
        case Serial16550::LineConfig::Stop::One:          dcb.StopBits = ONESTOPBIT;   break;
        case Serial16550::LineConfig::Stop::OnePointFive: dcb.StopBits = ONE5STOPBITS; break;
        case Serial16550::LineConfig::Stop::Two:          dcb.StopBits = TWOSTOPBITS;  break;
    }
    dcb.fBinary = TRUE;
    dcb.fParity = (c.parity != Serial16550::LineConfig::Parity::None);
    /* RTS/DTR are driven manually from the guest (OnControlLines) and the host's
       CTS/DSR/DCD/RI are read back, so the driver's own flow control is off. */
    dcb.fOutxCtsFlow    = FALSE;
    dcb.fOutxDsrFlow    = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fDtrControl     = DTR_CONTROL_DISABLE;
    dcb.fRtsControl     = RTS_CONTROL_DISABLE;
    dcb.fInX            = FALSE;
    dcb.fOutX           = FALSE;
    dcb.fNull           = FALSE;
    dcb.fAbortOnError   = FALSE;
    SetCommState(handle_, &dcb);
}

void HostSerialForward::OnGuestTx(const uint8_t* data, size_t n) {
    if (handle_ == INVALID_HANDLE_VALUE || n == 0) return;
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        tx_buf_.insert(tx_buf_.end(), data, data + n);
    }
    tx_cv_.notify_one();
}

void HostSerialForward::OnControlLines(bool dtr, bool rts) {
    if (handle_ == INVALID_HANDLE_VALUE) return;
    EscapeCommFunction(handle_, dtr ? SETDTR : CLRDTR);
    EscapeCommFunction(handle_, rts ? SETRTS : CLRRTS);
}

void HostSerialForward::ReaderLoop() {
    uint8_t buf[512];
    while (running_.load()) {
        DWORD ms = 0;
        if (GetCommModemStatus(handle_, &ms)) {
            const uint8_t cur = (uint8_t)(ms & 0xF0u);   /* CTS/DSR/RI/DCD levels */
            if (cur != last_ms_) {
                last_ms_ = cur;
                if (uart_)
                    uart_->SetModemInputs((ms & MS_CTS_ON)  != 0,
                                          (ms & MS_DSR_ON)  != 0,
                                          (ms & MS_RING_ON) != 0,
                                          (ms & MS_RLSD_ON) != 0);
            }
        }
        DWORD got = 0;
        if (ReadFile(handle_, buf, sizeof buf, &got, nullptr) && got > 0 && uart_)
            uart_->PushRx(buf, got);
    }
}

void HostSerialForward::WriterLoop() {
    std::vector<uint8_t> local;
    while (true) {
        {
            std::unique_lock<std::mutex> lk(tx_mu_);
            tx_cv_.wait(lk, [this] { return !tx_buf_.empty() || !running_.load(); });
            if (!running_.load() && tx_buf_.empty()) return;
            local.swap(tx_buf_);
        }
        size_t off = 0;
        while (off < local.size() && running_.load()) {
            DWORD wrote = 0;
            if (!WriteFile(handle_, local.data() + off,
                           (DWORD)(local.size() - off), &wrote, nullptr) ||
                wrote == 0)
                break;
            off += wrote;
        }
        local.clear();
    }
}
