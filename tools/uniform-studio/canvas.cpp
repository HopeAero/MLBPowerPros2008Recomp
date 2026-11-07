// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas.h"

#include "lang.h"
#include "ui.h"

#include <algorithm>
#include <cstdint>

namespace ustudio {
using power_pros_ui::Tr;
namespace ui = tstudio::ui;

namespace {
ImTextureID Tex(SDL_Texture* t) { return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(t)); }

// The drawn bounding box of a layer, in atlas pixels (text size is approximated for hit-testing).
void Bounds(const Layer& L, float* w, float* h) {
    if (L.isText) {
        *w = L.fontPx * 0.55f * std::max<size_t>(1, L.text.size());
        *h = L.fontPx;
    } else {
        *w = L.DrawnW();
        *h = L.DrawnH();
    }
}
} // namespace

void DrawCanvas(Canvas& c, SDL_Texture* composed, SDL_Texture* guide, ImVec2 size) {
    const KindInfo& in = Info(c.kind);
    if (!ui::BeginPanel("##canvas", Tr("Molde del uniforme", "Uniform mould"), size)) {
        ui::EndPanel();
        return;
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + avail.x, a.y + avail.y);
    const auto [p0, p1] = ui::Fit(a, b, static_cast<float>(in.w), static_cast<float>(in.h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ui::Checker(dl, p0, p1);
    if (guide) dl->AddImage(Tex(guide), p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 80));
    if (composed) dl->AddImage(Tex(composed), p0, p1);
    dl->AddRect(p0, p1, ui::kPanelBorder, 0, 0, 2);
    const float zoom = (p1.x - p0.x) / in.w;

    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##cv", ImVec2(p1.x - p0.x, p1.y - p0.y));
    const ImVec2 m = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated()) {
        const float ax = (m.x - p0.x) / zoom, ay = (m.y - p0.y) / zoom;
        c.selected = -1;
        for (int i = static_cast<int>(c.layers.size()) - 1; i >= 0; --i) {
            const Layer& L = c.layers[i];
            if (!L.visible) continue;
            float w, h;
            Bounds(L, &w, &h);
            if (ax >= L.x && ay >= L.y && ax <= L.x + w && ay <= L.y + h) { c.selected = i; break; }
        }
    }
    if (ImGui::IsItemActive() && c.selected >= 0 && c.selected < static_cast<int>(c.layers.size())) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        c.layers[c.selected].x += d.x / zoom;
        c.layers[c.selected].y += d.y / zoom;
    }
    if (c.selected >= 0 && c.selected < static_cast<int>(c.layers.size())) {
        const Layer& L = c.layers[c.selected];
        float w, h;
        Bounds(L, &w, &h);
        dl->AddRect(ImVec2(p0.x + L.x * zoom, p0.y + L.y * zoom), ImVec2(p0.x + (L.x + w) * zoom, p0.y + (L.y + h) * zoom),
                    ui::kYellow, 0, 0, 2);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("Arrastra para mover la capa seleccionada.", "Drag to move the selected layer."));
    ui::EndPanel();
}

} // namespace ustudio
