// ui/scale.cpp -- see ui/scale.h.

#include "ui/scale.h"

#include <cmath>

namespace ui::scale {
namespace {

// Reference design height: all ui/ pixel constants were authored at 1080p.
constexpr float kRefHeight = 1080.f;

// Clamp so a tiny window can't shrink the UI unreadable and an 8K screen
// can't explode the atlas (glyph size grows quadratically in texture area).
constexpr float kMin = 0.5f;
constexpr float kMax = 3.0f;

float g_scale = 1.0f;   // render-thread only
bool  g_rebuild = false;

}  // namespace

void NoteViewport(float width, float height) {
    (void)width;  // height drives the factor (widescreen shouldn't inflate text)
    if (height <= 0.f) return;
    float raw = height / kRefHeight;
    if (raw < kMin) raw = kMin;
    if (raw > kMax) raw = kMax;
    // Quantize to sixths: 720/1080/1440/2160 all land exactly, and a windowed
    // drag-resize crosses a step (and re-bakes the atlas) only a few times.
    const float q = std::round(raw * 6.f) / 6.f;
    if (q != g_scale) {
        g_scale = q;
        g_rebuild = true;
    }
}

float Ui() { return g_scale; }

void RequestRebuild() { g_rebuild = true; }

bool ConsumeRebuild() {
    const bool r = g_rebuild;
    g_rebuild = false;
    return r;
}

}  // namespace ui::scale
