#include "frame_renderer.h"
#include "guest_additions_frame_renderer.h"
#include "panel_frame_renderer.h"
#include "../core/cerf_emulator.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace {

class PresentedFrameRenderer : public FrameRenderer {
public:
    using FrameRenderer::FrameRenderer;

    bool HasFrame() override {
        auto* ga = emu_.TryGet<GuestAdditionsFrameRenderer>();
        if (ga && ga->HasFrame()) return true;
        auto* panel = emu_.TryGet<PanelFrameRenderer>();
        return panel && panel->HasFrame();
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        if (auto* ga = emu_.TryGet<GuestAdditionsFrameRenderer>()) {
            ga->PresentedSize(w, h);
            return;
        }
        if (auto* panel = emu_.TryGet<PanelFrameRenderer>()) {
            panel->PresentedSize(w, h);
            return;
        }
        w = 0;
        h = 0;
    }

    void RenderInto(uint32_t* dib_bgra32,
                    uint32_t  width,
                    uint32_t  height) override {
        auto* ga = emu_.TryGet<GuestAdditionsFrameRenderer>();
        if (ga && ga->HasFrame()) {
            ga->RenderInto(dib_bgra32, width, height);
            return;
        }

        auto* panel = emu_.TryGet<PanelFrameRenderer>();
        if (!panel || !panel->HasFrame()) {
            std::memset(dib_bgra32, 0, (size_t)width * height * 4u);
            return;
        }
        if (!ga) {
            panel->RenderInto(dib_bgra32, width, height);
            return;
        }
        ComposeLayer(dib_bgra32, width, height, *panel);
    }

    void RearmContentLatch() override {
        if (auto* ga = emu_.TryGet<GuestAdditionsFrameRenderer>())
            ga->RearmContentLatch();
        if (auto* panel = emu_.TryGet<PanelFrameRenderer>())
            panel->RearmContentLatch();
    }

    std::optional<FbLayout> GetFbLayout() override {
        auto* ga = emu_.TryGet<GuestAdditionsFrameRenderer>();
        if (ga && ga->HasFrame()) return ga->GetFbLayout();
        if (auto* panel = emu_.TryGet<PanelFrameRenderer>())
            return panel->GetFbLayout();
        return std::nullopt;
    }

private:
    void ComposeLayer(uint32_t* dst, uint32_t dst_w, uint32_t dst_h,
                      FrameRenderer& layer) {
        uint32_t lw = 0, lh = 0;
        layer.PresentedSize(lw, lh);
        if (lw == 0 || lh == 0) {
            std::memset(dst, 0, (size_t)dst_w * dst_h * 4u);
            return;
        }
        if (lw == dst_w && lh == dst_h) {
            layer.RenderInto(dst, dst_w, dst_h);
            return;
        }

        if (lw != scratch_w_ || lh != scratch_h_) {
            scratch_.assign((size_t)lw * lh, 0u);
            scratch_w_ = lw;
            scratch_h_ = lh;
        }
        layer.RenderInto(scratch_.data(), lw, lh);

        const uint32_t fit_x = dst_w / lw;
        const uint32_t fit_y = dst_h / lh;
        uint32_t scale = (fit_x < fit_y) ? fit_x : fit_y;
        if (scale == 0) scale = 1;

        const uint32_t out_w  = lw * scale;
        const uint32_t out_h  = lh * scale;
        const uint32_t copy_w = (out_w < dst_w) ? out_w : dst_w;
        const uint32_t copy_h = (out_h < dst_h) ? out_h : dst_h;
        const uint32_t dst_x  = (dst_w - copy_w) / 2u;
        const uint32_t dst_y  = (dst_h - copy_h) / 2u;
        const uint32_t src_x  = (out_w - copy_w) / 2u;
        const uint32_t src_y  = (out_h - copy_h) / 2u;

        std::memset(dst, 0, (size_t)dst_w * dst_h * 4u);

        const uint32_t* prev_src_row = nullptr;
        const uint32_t* prev_dst_row = nullptr;
        for (uint32_t y = 0; y < copy_h; ++y) {
            const uint32_t* src_row =
                scratch_.data() + (size_t)((src_y + y) / scale) * lw;
            uint32_t* dst_row = dst + (size_t)(dst_y + y) * dst_w + dst_x;
            if (src_row == prev_src_row) {
                std::memcpy(dst_row, prev_dst_row, (size_t)copy_w * 4u);
                continue;
            }
            if (scale == 1u) {
                std::memcpy(dst_row, src_row + src_x, (size_t)copy_w * 4u);
            } else {
                for (uint32_t x = 0; x < copy_w; ++x)
                    dst_row[x] = src_row[(src_x + x) / scale];
            }
            prev_src_row = src_row;
            prev_dst_row = dst_row;
        }
    }

    std::vector<uint32_t> scratch_;
    uint32_t scratch_w_ = 0;
    uint32_t scratch_h_ = 0;
};

}

REGISTER_SERVICE_AS(PresentedFrameRenderer, FrameRenderer);
