// SPDX-License-Identifier: GPL-3.0-or-later
// The preview panel: a rotatable 3D mannequin wearing the composed uniform, or the flat composed atlas.
// A toggle switches them. Also shows the palette warning when an atlas needs more than 256 colours.
#pragma once
#include "layers.h"

#include <imgui.h>

struct SDL_Texture;

namespace ustudio {
// `render3d` is the software-rendered mannequin texture; `body` the composed atlas (flat view); `colors`
// its distinct-colour count. `*figure` picks the 3D view (true) or the flat atlas (false); dragging the
// 3D view updates *yaw/*pitch and the wheel updates *zoom.
void DrawPreview(SDL_Texture* render3d, SDL_Texture* body, SDL_Texture* guide, ImVec2 size, int colors,
                 bool* figure, float* yaw, float* pitch, float* zoom);
} // namespace ustudio
