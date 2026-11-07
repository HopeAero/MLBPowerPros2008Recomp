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
} // namespace

void DrawPreview(SDL_Texture* body, SDL_Texture* guide, ImVec2 size, int colors) {
    if (!ui::BeginPanel("##preview", Tr("Vista previa", "Preview"), size)) {
        ui::EndPanel();
        return;
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float warnH = 30.0f;
    const ImVec2 b(a.x + avail.x, a.y + avail.y - warnH);
    const auto [p0, p1] = ui::Fit(a, b, 128.0f, 128.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ui::Checker(dl, p0, p1);
    if (guide) dl->AddImage(Tex(guide), p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 45));
    if (body) dl->AddImage(Tex(body), p0, p1);
    dl->AddRect(p0, p1, ui::kPanelBorder, 0, 0, 2);

    // Palette warning / colour count (step 4).
    char msg[128];
    const bool over = colors > 255;
    std::snprintf(msg, sizeof msg,
                  over ? Tr("Aviso: %d colores (>256); se cuantizará", "Warning: %d colours (>256); it will be quantised")
                       : Tr("%d colores (cabe en la paleta de 256)", "%d colours (fits the 256 palette)"),
                  colors);
    power_pros_ui::ShadowedText(dl, power_pros_ui::g_bold, 15.0f, ImVec2(a.x + 4, a.y + avail.y - warnH + 6),
                                over ? ui::kOrangeTop : ui::kMuted, msg);
    ui::EndPanel();
}

} // namespace ustudio
