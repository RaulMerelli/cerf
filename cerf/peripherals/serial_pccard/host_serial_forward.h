#pragma once

#include "serial_endpoint.h"
#include "serial_16550.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class EmulationFreeze;

/* SerialEndpoint that bridges the guest's 16550 to a real host COM port: raw
   bytes and control/status lines pass through both ways; the host port's baud +
   framing track the guest's Serial16550::LineConfig. No AT / no modem logic. */
class HostSerialForward : public SerialEndpoint {
public:
    HostSerialForward(std::wstring host_port, EmulationFreeze& freeze);
    ~HostSerialForward() override;

    void OnGuestTx(const uint8_t* data, size_t n) override;
    void OnControlLines(bool dtr, bool rts) override;
    void OnOpen()  override;
    void OnClose() override;

private:
    void ReaderLoop();
    void WriterLoop();
    void ApplyLineConfig(const Serial16550::LineConfig& c);
    void StopThreads();

    EmulationFreeze&  freeze_;
    std::wstring      port_name_;        /* e.g. "COM3" */
    HANDLE            handle_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> running_{false};

    std::thread reader_;
    std::thread writer_;

    std::mutex              tx_mu_;
    std::condition_variable tx_cv_;
    std::vector<uint8_t>    tx_buf_;     /* guest TX awaiting WriteFile */

    uint8_t last_ms_ = 0xFFu;            /* last host modem-status pushed to UART */
};
