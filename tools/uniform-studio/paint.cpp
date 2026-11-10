// SPDX-License-Identifier: GPL-3.0-or-later
#include "paint.h"

#include <algorithm>
#include <cmath>

namespace ustudio {
using tstudio::Image;

bool PaintMask::Allows(const Image& img, int x, int y) const {
    if (x < 0 || y < 0 || x >= static_cast<int>(img.width) || y >= static_cast<int>(img.height)) return false;
    return !mask || mask->size() != static_cast<size_t>(img.width) * img.height ||
           (*mask)[static_cast<size_t>(y) * img.width + x] != 0;
}

namespace {
void Put(Image& img, int x, int y, Rgba8 c, bool erase, const PaintMask& m) {
    if (!m.Allows(img, x, y)) return;
    uint8_t* p = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
    if (erase) { p[0] = p[1] = p[2] = p[3] = 0; return; }
    p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = c[3];
}
}  // namespace

void PaintDab(Image& img, float x, float y, float size, Rgba8 c, bool erase, const PaintMask& m) {
    const float r = std::max(0.5f, size / 2);
    for (int py = static_cast<int>(std::floor(y - r)); py <= static_cast<int>(std::ceil(y + r)); ++py)
        for (int px = static_cast<int>(std::floor(x - r)); px <= static_cast<int>(std::ceil(x + r)); ++px) {
            const float dx = px + 0.5f - x, dy = py + 0.5f - y;
            if (dx * dx + dy * dy <= r * r) Put(img, px, py, c, erase, m);
        }
}

void PaintLine(Image& img, float x0, float y0, float x1, float y1, float size, Rgba8 c, bool erase, const PaintMask& m) {
    const float len = std::hypot(x1 - x0, y1 - y0);
    const int steps = std::max(1, static_cast<int>(std::ceil(len / std::max(0.5f, size / 3))));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        PaintDab(img, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, size, c, erase, m);
    }
}

void PaintRect(Image& img, float x0, float y0, float x1, float y1, float size, bool filled, Rgba8 c, const PaintMask& m) {
    const int ax = static_cast<int>(std::floor(std::min(x0, x1))), bx = static_cast<int>(std::ceil(std::max(x0, x1)));
    const int ay = static_cast<int>(std::floor(std::min(y0, y1))), by = static_cast<int>(std::ceil(std::max(y0, y1)));
    const int t = std::max(1, static_cast<int>(std::lround(size)));
    for (int y = ay; y < by; ++y)
        for (int x = ax; x < bx; ++x)
            if (filled || x < ax + t || x >= bx - t || y < ay + t || y >= by - t) Put(img, x, y, c, false, m);
}

void PaintFill(Image& img, const Image* ref, int x, int y, Rgba8 c, const PaintMask& m) {
    const int w = static_cast<int>(img.width), h = static_cast<int>(img.height);
    if (!ref || ref->width != img.width || ref->height != img.height) ref = &img;
    if (!m.Allows(img, x, y)) return;
    const size_t start = (static_cast<size_t>(y) * w + x) * 4;
    const Rgba8 from{ref->rgba[start], ref->rgba[start + 1], ref->rgba[start + 2], ref->rgba[start + 3]};
    if (ref == &img && from == c) return;
    auto same = [&](int px, int py) {
        const uint8_t* p = &ref->rgba[(static_cast<size_t>(py) * w + px) * 4];
        return p[0] == from[0] && p[1] == from[1] && p[2] == from[2] && p[3] == from[3];
    };
    std::vector<uint8_t> seen(static_cast<size_t>(w) * h, 0);
    std::vector<std::pair<int, int>> stack{{x, y}};
    while (!stack.empty()) {
        auto [px, py] = stack.back();
        stack.pop_back();
        if (px < 0 || py < 0 || px >= w || py >= h) continue;
        uint8_t& s = seen[static_cast<size_t>(py) * w + px];
        if (s || !m.Allows(img, px, py) || !same(px, py)) continue;
        s = 1;
        Put(img, px, py, c, false, m);
        stack.push_back({px + 1, py});
        stack.push_back({px - 1, py});
        stack.push_back({px, py + 1});
        stack.push_back({px, py - 1});
    }
}

void PaintPattern(Image& img, Pattern p, float width, float spacing, float offset, Rgba8 c, const PaintMask& m) {
    spacing = std::max(1.0f, spacing);
    width = std::clamp(width, 0.5f, spacing);
    for (int y = 0; y < static_cast<int>(img.height); ++y)
        for (int x = 0; x < static_cast<int>(img.width); ++x) {
            const float cx = x + 0.5f, cy = y + 0.5f;
            bool on = false;
            auto band = [&](float v) { return std::fmod(std::fmod(v - offset, spacing) + spacing, spacing) < width; };
            switch (p) {
            case Pattern::Vertical: on = band(cx); break;
            case Pattern::Horizontal: on = band(cy); break;
            case Pattern::Diagonal: on = band((cx + cy) * 0.7071f); break;
            case Pattern::DiagonalBack: on = band((cx - cy) * 0.7071f); break;
            case Pattern::Checks: {
                const int i = static_cast<int>(std::floor((cx - offset) / spacing)), j = static_cast<int>(std::floor((cy - offset) / spacing));
                on = ((i + j) & 1) == 0;
                break;
            }
            }
            if (on) Put(img, x, y, c, false, m);
        }
}

}  // namespace ustudio
