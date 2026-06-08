#pragma once

#include "../core/service.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/* Types a board's ASCII-art logo + a loading message into the UartScreen
   boot tab, centered on screen. A concrete subclass supplies the logo and
   gates itself to one board; the typing/centering/reset wiring is shared. */
class TextModeBootBanner : public Service {
public:
    using Service::Service;
    ~TextModeBootBanner() override;

    void OnReady() override;
    void OnShutdown() override;

    /* Any thread. New native/LCD resolution: re-run the typing animation,
       re-centered on the new size. No-op if the size is unchanged. */
    void OnScreenSizeChanged(uint32_t width, uint32_t height);

protected:
    /* The board logo as text-mode art. Left-justified with no outer
       centering padding (the base centers on screen); internal relative
       indentation that aligns elements is part of the logo's own shape. */
    virtual std::vector<std::string> LogoLines() const = 0;

private:
    void StartAnimation();
    void StopAnimation();
    void StopLocked();
    void Render();
    bool WaitOrStopped(std::chrono::milliseconds d);

    std::atomic<uint32_t> screen_w_{0};
    std::atomic<uint32_t> screen_h_{0};

    std::mutex              ctl_mutex_;   /* serializes start/stop/reprint */
    std::thread             anim_;

    std::mutex              stop_mutex_;
    std::condition_variable stop_cv_;
    bool                    stop_ = false;   /* guarded by stop_mutex_ */

    std::mutex   msg_mutex_;
    std::string  boot_message_{"Starting Windows CE..."};  /* guarded by msg_mutex_ */

    /* Count of content lines already typed; the resume point across
       resolution-change / reboot restarts. Touched only by the (single)
       animation thread, which restarts serialize behind StopLocked. */
    std::size_t  progress_ = 0;
};
