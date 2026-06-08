#define NOMINMAX

#include "text_mode_boot_banner.h"

#include "../core/cerf_emulator.h"
#include "../socs/guest_cpu_reset.h"
#include "initial_window_size.h"
#include "uart_screen.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

constexpr auto kLineInterval = std::chrono::milliseconds(25);

/* UartScreen's Fixedsys boot-log font has a fixed 8x16 px glyph cell. */
constexpr int kCellW = 8;
constexpr int kCellH = 16;

}  /* namespace */

TextModeBootBanner::~TextModeBootBanner() { StopAnimation(); }

/* Animation thread renders into UartScreen; stop it before any peer is
   destroyed. */
void TextModeBootBanner::OnShutdown() { StopAnimation(); }

void TextModeBootBanner::OnReady() {
    const auto size = emu_.Get<InitialWindowSize>().Resolve();
    screen_w_.store(size.width,  std::memory_order_release);
    screen_h_.store(size.height, std::memory_order_release);
    StartAnimation();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this] {
        { std::lock_guard<std::mutex> lk(msg_mutex_); boot_message_ = "Rebooting..."; }
        StartAnimation();
    });
}

void TextModeBootBanner::OnScreenSizeChanged(uint32_t width, uint32_t height) {
    if (screen_w_.load(std::memory_order_acquire) == width &&
        screen_h_.load(std::memory_order_acquire) == height)
        return;
    screen_w_.store(width,  std::memory_order_release);
    screen_h_.store(height, std::memory_order_release);
    StartAnimation();
}

void TextModeBootBanner::StartAnimation() {
    std::lock_guard<std::mutex> lk(ctl_mutex_);
    StopLocked();
    { std::lock_guard<std::mutex> s(stop_mutex_); stop_ = false; }
    anim_ = std::thread([this] { Render(); });
}

void TextModeBootBanner::StopAnimation() {
    std::lock_guard<std::mutex> lk(ctl_mutex_);
    StopLocked();
}

void TextModeBootBanner::StopLocked() {
    { std::lock_guard<std::mutex> s(stop_mutex_); stop_ = true; }
    stop_cv_.notify_all();
    if (anim_.joinable()) anim_.join();
}

bool TextModeBootBanner::WaitOrStopped(std::chrono::milliseconds d) {
    std::unique_lock<std::mutex> lk(stop_mutex_);
    return stop_cv_.wait_for(lk, d, [this] { return stop_; });
}

void TextModeBootBanner::Render() {
    const std::vector<std::string> logo = LogoLines();
    std::string msg;
    { std::lock_guard<std::mutex> lk(msg_mutex_); msg = boot_message_; }

    constexpr int kGap = 4;  /* blank lines between logo and message */

    /* Logo art keeps its leading whitespace verbatim (boards hand-place
       elements within the block); the message centers on its own width. */
    size_t widest = 0;
    for (const std::string& l : logo)
        widest = std::max(widest, l.size());

    const int cols = (int)screen_w_.load(std::memory_order_acquire) / kCellW;
    const int rows = (int)screen_h_.load(std::memory_order_acquire) / kCellH;

    const int logo_left = std::max(0, (cols - (int)widest)    / 2);
    const int msg_left  = std::max(0, (cols - (int)msg.size()) / 2);
    const int n         = (int)logo.size() + kGap + 1;
    const int top       = std::max(0, (rows - n) / 2);

    auto& screen = emu_.Get<UartScreen>();
    const std::string logo_pad(logo_left, ' ');

    std::vector<std::string> content;
    content.reserve(logo.size() + kGap + 1);
    for (const std::string& l : logo) {
        if (l.find_first_not_of(' ') != std::string::npos)
            content.push_back(logo_pad + l);
        else
            content.emplace_back();
    }
    for (int i = 0; i < kGap; ++i) content.emplace_back();
    content.push_back(std::string(msg_left, ' ') + msg);

    /* Redraw the lines already revealed (at the current centering), then
       resume typing where a prior run left off. progress_ persists across
       resolution-change / reboot restarts so the animation never replays. */
    const size_t start = std::min(progress_, content.size());

    screen.Clear();
    for (int i = 0; i < top; ++i) screen.AddLine("");
    for (size_t i = 0; i < start; ++i) screen.AddLine(content[i]);
    for (size_t i = start; i < content.size(); ++i) {
        screen.AddLine(content[i]);
        progress_ = i + 1;
        if (WaitOrStopped(kLineInterval)) return;
    }
}
