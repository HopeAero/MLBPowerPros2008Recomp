// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview.h"

#include "lang.h"
#include "ui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace ustudio {
using power_pros_ui::Tr;
namespace ui = tstudio::ui;

namespace {
ImTextureID Tex(SDL_Texture* t) { return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(t)); }
} // namespace

void DrawPreview(SDL_Texture* render3d, SDL_Texture* body, SDL_Texture* guide, ImVec2 size, int colors,
                 bool* figure, float* yaw, float* pitch, float* zoom, bool* inspect) {
    if (!ui::BeginPanel("##preview", Tr("Vista previa", "Preview"), size)) {
        ui::EndPanel();
        return;
    }
    // View toggle.
    if (ui::Pill(Tr("Muñeco 3D", "3D figure"), *figure ? ui::kBlueTop : ui::kGreyTop,
                 *figure ? ui::kBlueBottom : ui::kGreyBottom, true))
        *figure = true;
    ImGui::SameLine();
    if (ui::Pill(Tr("Atlas", "Atlas"), !*figure ? ui::kBlueTop : ui::kGreyTop,
                 !*figure ? ui::kBlueBottom : ui::kGreyBottom, true))
        *figure = false;
    if (*figure) {
        ImGui::SameLine();
        if (ui::Pill(Tr("Ampliar", "Enlarge"), ui::kGreenTop, ui::kGreenBottom, true)) *inspect = true;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float warnH = 28.0f;
    const ImVec2 b(a.x + avail.x, a.y + avail.y - warnH);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (*figure) {
        const auto [p0, p1] = ui::Fit(a, b, 1.0f, 1.0f);
        dl->AddRectFilled(p0, p1, power_pros_ui::Rgba(14, 22, 38), 6);
        if (render3d) dl->AddImage(Tex(render3d), p0, p1);
        dl->AddRect(p0, p1, ui::kPanelBorder, 6, 0, 2);
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##orbit", ImVec2(p1.x - p0.x, p1.y - p0.y));
        if (ImGui::IsItemDeactivated() && ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x == 0.0f &&
            ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y == 0.0f)
            *inspect = true;  // a plain click opens the big viewer
        if (ImGui::IsItemActive()) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            *yaw += d.x * 0.012f;
            *pitch = std::clamp(*pitch + d.y * 0.012f, -1.2f, 1.2f);
        }
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            *zoom = std::clamp(*zoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f), 0.4f, 3.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Arrastra para girar · rueda para acercar · clic para ampliar",
                                       "Drag to rotate · wheel to zoom · click to enlarge"));
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
