#pragma once

#include "network_backend.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct Slirp;
struct SlirpCb;      /* libslirp callback vtable; heap-allocated so the
                        pointer libslirp stores stays valid for our lifetime */
struct SlirpTimer;

class SlirpBackend : public NetworkBackend {
public:
    using NetworkBackend::NetworkBackend;

    bool ShouldRegister() override;
    void OnReady() override;    /* construct libslirp + start thread */
    void OnShutdown() override;

    ~SlirpBackend() override;

    void SendFrame(const uint8_t* frame, std::size_t len) override;
    void SetReceiveCallback(RxFn cb) override;

    /* Phase 10.b - ICMPv4 echo intercept. libslirp's built-in ICMP path
       relies on SOCK_RAW on Windows (non-admin = WSAEACCES → silent ping
       failure). We intercept echo requests before slirp_input and
       service them with the unprivileged `IcmpSendEcho` Win32 API. */
    bool TryInterceptIcmpEcho(const uint8_t* frame, std::size_t len);

    bool TryInterceptAaaaQuery(const uint8_t* frame, std::size_t len);
    std::array<uint8_t, 6> GuestMacAddress() const override;
    std::array<uint8_t, 6> HostGatewayMacAddress() const override;

    /* Implementation hooks for the C callbacks libslirp invokes. Public so
       the static C-shim functions in the .cpp can call them; do not call
       these from outside the .cpp. */
    void OnSlirpSendPacket(const void* buf, std::size_t len);
    int64_t OnSlirpClockGetNs();
    SlirpTimer* OnSlirpTimerNew(void (*cb)(void* opaque), void* cb_opaque);
    void OnSlirpTimerFree(SlirpTimer* t);
    void OnSlirpTimerMod(SlirpTimer* t, int64_t expire_ms);
    void OnSlirpNotify();

private:
    void PollLoop();
    void StopPollThread();
    int64_t NowMs() const;

    std::array<uint8_t, 6> guest_mac_{};
    uint32_t mtu_ = 1500;
    bool host_has_v6_ = false;        /* set by IPv6 reachability probe in OnInit */

    Slirp* slirp_ = nullptr;
    SlirpCb* cbs_ = nullptr;
    std::mutex slirp_mutex_;          /* serializes ALL libslirp calls */

    RxFn rx_cb_;
    std::mutex rx_cb_mutex_;          /* protects rx_cb_ install/swap */

    std::thread poll_thread_;
    std::atomic<bool> stop_{false};
    std::mutex notify_mutex_;
    std::condition_variable notify_cv_;
    bool notified_ = false;

    std::mutex timers_mutex_;
    std::vector<std::unique_ptr<SlirpTimer>> timers_;
};
