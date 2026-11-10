// SPDX-License-Identifier: GPL-3.0-or-later
// The editing canvas: the UV mould behind the live-composited atlas, zoomed to the chosen zone, with
// click-to-select, drag to move, handles to resize (corners) or stretch (sides), the wheel to scale and
// the arrow keys to nudge.
#pragma once
#include "layers.h"

#include <imgui.h>

struct SDL_Texture;

namespace ustudio {
// The part of the atlas the canvas shows, in atlas pixels.
struct View {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // x1 == 0: the whole atlas
};

// Zoom and pan of the canvas over the view (zoom 1 = the whole view fits).
struct CanvasNav {
    float zoom = 1.0f;
    float panX = 0.0f, panY = 0.0f;  // atlas pixels from the view's centre
};

// Drawing on the canvas (paint.h) instead of moving layers.
enum class Tool { Brush, Line, Rect, RectFilled, Fill, Eraser, Picker };
struct PaintTool {
    Tool tool = Tool::Brush;
    float size = 2.0f;                       // brush / line / outline width, atlas pixels
    unsigned char color[4] = {255, 255, 255, 255};
    const std::vector<uint8_t>* mask = nullptr;  // the zone (atlas-sized), null = everywhere
    const Image* composed = nullptr;         // what the picker reads
};

// The canvas's drawing layer: the selected one if it is a drawing, else the top-most drawing, else a new
// atlas-sized transparent one on top (named `name`).
Layer& PaintLayer(Canvas& c, const char* name);

// Draws the mould (checker + region guide) with the composed atlas on top, scaled to fit `view`, and
// lets the player pick and edit a layer. `composed`, `guide` and `shade` are atlas-resolution textures
// (guide / shade may be null; shade darkens what is outside the zone).
// Draws at the cursor in an area of `size` (the caller owns the panel and its zoom buttons). Ctrl + wheel
// (or the wheel with nothing selected) zooms and the right or middle button pans. With `paint` the left
// button draws with that tool instead (the wheel changes the brush size).
void DrawCanvas(Canvas& c, SDL_Texture* composed, SDL_Texture* guide, SDL_Texture* shade, ImVec2 size, View view,
                CanvasNav& nav, PaintTool* paint = nullptr);
} // namespace ustudio
