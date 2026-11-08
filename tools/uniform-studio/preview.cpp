// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview.h"

#include "lang.h"
#include "ui.h"

#include <cstdint>
#include <cstdio>

namespace ustudio {
using power_pros_ui::Tr;
namespace ui = tstudio::ui;

namespace {
ImTextureID Tex(SDL_Texture* t) { return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(t)); }

// Draw one body part: a grey base (so an empty uniform still reads as a doll), the atlas sub-rect mapped
// onto it, and a dark outline. uv0/uv1 are normalised atlas coordinates.
void Part(ImDrawList* dl, SDL_Texture* body, ImVec2 a, ImVec2 b, ImVec2 uv0, ImVec2 uv1) {
    dl->AddRectFilled(a, b, IM_COL32(205, 210, 220, 255), 3);
    if (body) dl->AddImage(Tex(body), a, b, uv0, uv1);
    dl->AddRect(a, b, IM_COL32(20, 30, 50, 220), 3, 0, 1.5f);
}

// A chibi (big head) front paper-doll wearing the composed uniform. Approximate, not a 3D render.
void DrawDoll(ImDrawList* dl, SDL_Texture* body, ImVec2 c0, ImVec2 c1) {
    const float boxH = c1.y - c0.y;
    const float cx = (c0.x + c1.x) * 0.5f, top = c0.y + boxH * 0.03f, u = boxH * 0.1f;
    const ImVec2 chest0(0.0f, 0.0f), chest1(0.656f, 0.469f);
    const ImVec2 slv0(0.0f, 0.859f), slv1(0.656f, 1.0f);
    const ImVec2 pnt0(0.0f, 0.469f), pnt1(0.4375f, 0.859f);
    const ImVec2 cap0(0.672f, 0.0f), cap1(1.0f, 0.5f);
    // Legs.
    Part(dl, body, ImVec2(cx - 1.5f * u, top + 5.8f * u), ImVec2(cx - 0.15f * u, top + 8.6f * u), pnt0, pnt1);
    Part(dl, body, ImVec2(cx + 0.15f * u, top + 5.8f * u), ImVec2(cx + 1.5f * u, top + 8.6f * u), pnt0, pnt1);
    // Arms (right arm mirrors the sleeve UV).
    Part(dl, body, ImVec2(cx - 2.3f * u, top + 2.9f * u), ImVec2(cx - 1.55f * u, top + 5.4f * u), slv0, slv1);
    Part(dl, body, ImVec2(cx + 1.55f * u, top + 2.9f * u), ImVec2(cx + 2.3f * u, top + 5.4f * u), ImVec2(slv1.x, slv0.y), ImVec2(slv0.x, slv1.y));
    // Torso (chest).
    Part(dl, body, ImVec2(cx - 1.6f * u, top + 2.7f * u), ImVec2(cx + 1.6f * u, top + 5.9f * u), chest0, chest1);
    // Head (skin) and cap.
    const ImVec2 head(cx, top + 1.55f * u);
    dl->AddCircleFilled(head, 1.25f * u, IM_COL32(242, 203, 170, 255), 32);
    dl->AddCircle(head, 1.25f * u, IM_COL32(20, 30, 50, 220), 32, 1.5f);
    Part(dl, body, ImVec2(cx - 1.45f * u, top + 0.15f * u), ImVec2(cx + 1.45f * u, top + 1.35f * u), cap0, cap1);
}
} // namespace

void DrawPreview(SDL_Texture* body, SDL_Texture* guide, ImVec2 size, int colors, bool* dollView) {
    if (!ui::BeginPanel("##preview", Tr("Vista previa", "Preview"), size)) {
        ui::EndPanel();
        return;
    }
    // View toggle.
    if (ui::Pill(Tr("Muñeco", "Figure"), *dollView ? ui::kBlueTop : ui::kGreyTop,
                 *dollView ? ui::kBlueBottom : ui::kGreyBottom, true))
        *dollView = true;
    ImGui::SameLine();
    if (ui::Pill(Tr("Atlas", "Atlas"), !*dollView ? ui::kBlueTop : ui::kGreyTop,
                 !*dollView ? ui::kBlueBottom : ui::kGreyBottom, true))
        *dollView = false;

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float warnH = 28.0f;
    const ImVec2 b(a.x + avail.x, a.y + avail.y - warnH);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (*dollView) {
        DrawDoll(dl, body, a, b);
    } else {
        const auto [p0, p1] = ui::Fit(a, b, 128.0f, 128.0f);
        ui::Checker(dl, p0, p1);
        if (guide) dl->AddImage(Tex(guide), p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 45));
        if (body) dl->AddImage(Tex(body), p0, p1);
        dl->AddRect(p0, p1, ui::kPanelBorder, 0, 0, 2);
    }

    char msg[128];
    const bool over = colors > 255;
    std::snprintf(msg, sizeof msg,
                  over ? Tr("Aviso: %d colores (>256); se cuantizará", "Warning: %d colours (>256); it will be quantised")
                       : Tr("%d colores (cabe en la paleta de 256)", "%d colours (fits the 256 palette)"),
                  colors);
    power_pros_ui::ShadowedText(dl, power_pros_ui::g_bold, 14.0f, ImVec2(a.x + 4, a.y + avail.y - warnH + 6),
                                over ? ui::kOrangeTop : ui::kMuted, msg);
    ui::EndPanel();
}

} // namespace ustudio
