// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas.h"

#include "lang.h"
#include "paint.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
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

// Handles of the selection box: 0..3 corners (TL, TR, BR, BL) resize keeping the shape, 4..7 sides
// (top, right, bottom, left) stretch one way, 8 the knob above the top side turns an image. -1 = none.
constexpr int kRotateHandle = 8;
constexpr float kKnobGap = 22.0f;  // screen pixels between the top side and the turning knob
// A handle's place in the layer's own box (0..w, 0..h).
ImVec2 HandleLocal(int k, float w, float h) {
    switch (k) {
    case 0: return ImVec2(0, 0);
    case 1: return ImVec2(w, 0);
    case 2: return ImVec2(w, h);
    case 3: return ImVec2(0, h);
    case 4: return ImVec2(w / 2, 0);
    case 5: return ImVec2(w, h / 2);
    case 6: return ImVec2(w / 2, h);
    default: return ImVec2(0, h / 2);
    }
}

// A layer's box turned `rot` degrees (clockwise, y down) about its centre: its own coordinates
// (0..w, 0..h) <-> atlas pixels. Text is never turned.
struct Frame {
    float cx = 0, cy = 0, w = 0, h = 0, c = 1, s = 0;
    ImVec2 ToAtlas(float lx, float ly) const {
        const float dx = lx - w / 2, dy = ly - h / 2;
        return ImVec2(cx + c * dx - s * dy, cy + s * dx + c * dy);
    }
    ImVec2 ToLocal(float ax, float ay) const {
        const float dx = ax - cx, dy = ay - cy;
        return ImVec2(c * dx + s * dy + w / 2, -s * dx + c * dy + h / 2);
    }
};
Frame FrameOf(const Layer& L) {
    Frame f;
    Bounds(L, &f.w, &f.h);
    f.cx = L.x + f.w / 2;
    f.cy = L.y + f.h / 2;
    const float r = L.isText ? 0.0f : L.rot * 3.14159265f / 180.0f;
    f.c = std::cos(r);
    f.s = std::sin(r);
    return f;
}

struct Drag {
    int handle = -1;              // the handle being dragged, -1 = moving the layer
    Frame f;                      // the layer's box when the drag started (atlas pixels)
    float scale = 1, sx = 1, sy = 1, fontPx = 20;
} g_drag;

struct Stroke {
    bool on = false;
    float x0 = 0, y0 = 0, lx = 0, ly = 0;  // where it started, the last brush point (atlas pixels)
} g_stroke;

// The left button draws with the tool; a line / rectangle shows while dragging and lands on release.
void PaintOnCanvas(Canvas& c, PaintTool& t, View view, ImVec2 p0, float zoom, ImDrawList* dl) {
    const ImGuiIO& io = ImGui::GetIO();
    const float ax = view.x0 + (io.MousePos.x - p0.x) / zoom, ay = view.y0 + (io.MousePos.y - p0.y) / zoom;
    auto toScreen = [&](float x, float y) { return ImVec2(p0.x + (x - view.x0) * zoom, p0.y + (y - view.y0) * zoom); };
    const PaintMask m{t.mask};
    const Rgba8 col{t.color[0], t.color[1], t.color[2], t.color[3]};
    const bool brush = t.tool == Tool::Brush || t.tool == Tool::Eraser, erase = t.tool == Tool::Eraser;
    const bool shape = t.tool == Tool::Line || t.tool == Tool::Rect || t.tool == Tool::RectFilled;
    const char* name = Tr("Dibujo", "Drawing");
    auto pick = [&] {
        const int x = static_cast<int>(std::floor(ax)), y = static_cast<int>(std::floor(ay));
        const Image* img = t.composed;
        if (!img || x < 0 || y < 0 || x >= static_cast<int>(img->width) || y >= static_cast<int>(img->height)) return;
        const uint8_t* p = &img->rgba[(static_cast<size_t>(y) * img->width + x) * 4];
        if (p[3] == 0) return;
        t.color[0] = p[0]; t.color[1] = p[1]; t.color[2] = p[2]; t.color[3] = 255;
    };
    if (ImGui::IsItemActivated()) {
        g_stroke = {true, ax, ay, ax, ay};
        if (t.tool == Tool::Picker) pick();
        else if (t.tool == Tool::Fill)
            PaintFill(PaintLayer(c, name).src, t.composed, static_cast<int>(std::floor(ax)), static_cast<int>(std::floor(ay)), col, m);
        else if (brush) PaintDab(PaintLayer(c, name).src, ax, ay, t.size, col, erase, m);
    }
    if (ImGui::IsItemActive() && g_stroke.on) {
        if (t.tool == Tool::Picker) pick();
        if (brush && (ax != g_stroke.lx || ay != g_stroke.ly)) {
            PaintLine(PaintLayer(c, name).src, g_stroke.lx, g_stroke.ly, ax, ay, t.size, col, erase, m);
            g_stroke.lx = ax;
            g_stroke.ly = ay;
        }
        if (shape) {  // preview
            const ImU32 u = IM_COL32(col[0], col[1], col[2], 220);
            const ImVec2 s0 = toScreen(g_stroke.x0, g_stroke.y0), s1 = toScreen(ax, ay);
            if (t.tool == Tool::Line) dl->AddLine(s0, s1, u, std::max(1.0f, t.size * zoom));
            else if (t.tool == Tool::Rect) dl->AddRect(s0, s1, u, 0, 0, std::max(1.0f, t.size * zoom));
            else dl->AddRectFilled(ImVec2(std::min(s0.x, s1.x), std::min(s0.y, s1.y)), ImVec2(std::max(s0.x, s1.x), std::max(s0.y, s1.y)), u);
        }
    }
    if (ImGui::IsItemDeactivated() && g_stroke.on) {
        if (t.tool == Tool::Line) PaintLine(PaintLayer(c, name).src, g_stroke.x0, g_stroke.y0, ax, ay, t.size, col, false, m);
        if (t.tool == Tool::Rect || t.tool == Tool::RectFilled)
            PaintRect(PaintLayer(c, name).src, g_stroke.x0, g_stroke.y0, ax, ay, t.size, t.tool == Tool::RectFilled, col, m);
        g_stroke.on = false;
    }
    if (ImGui::IsItemHovered()) {  // the brush outline under the cursor
        const float r = std::max(1.5f, t.size * zoom / 2);
        if (brush || t.tool == Tool::Line || t.tool == Tool::Rect) {
            dl->AddCircle(io.MousePos, r + 1, IM_COL32(0, 0, 0, 200), 0, 1.5f);
            dl->AddCircle(io.MousePos, r, IM_COL32_WHITE, 0, 1.0f);
        } else {
            dl->AddLine(ImVec2(io.MousePos.x - 6, io.MousePos.y), ImVec2(io.MousePos.x + 6, io.MousePos.y), IM_COL32_WHITE, 1.5f);
            dl->AddLine(ImVec2(io.MousePos.x, io.MousePos.y - 6), ImVec2(io.MousePos.x, io.MousePos.y + 6), IM_COL32_WHITE, 1.5f);
        }
    }
}
}  // namespace

Layer& PaintLayer(Canvas& c, const char* name) {
    if (c.selected >= 0 && c.selected < static_cast<int>(c.layers.size()) && c.layers[c.selected].isPaint)
        return c.layers[c.selected];
    for (int i = static_cast<int>(c.layers.size()) - 1; i >= 0; --i)
        if (c.layers[i].isPaint) return c.layers[i];
    const KindInfo& in = Info(c.kind);
    Layer L;
    L.name = name;
    L.isPaint = true;
    L.src = Image{static_cast<uint32_t>(in.w), static_cast<uint32_t>(in.h),
                  std::vector<uint8_t>(static_cast<size_t>(in.w) * in.h * 4, 0)};
    c.layers.push_back(std::move(L));
    return c.layers.back();
}

void DrawCanvas(Canvas& c, SDL_Texture* composed, SDL_Texture* guide, SDL_Texture* shade, ImVec2 size, View view,
                CanvasNav& nav, PaintTool* paint) {
    const KindInfo& in = Info(c.kind);
    if (view.x1 <= view.x0 || view.y1 <= view.y0) view = {0, 0, static_cast<float>(in.w), static_cast<float>(in.h)};
    // The view zoomed / panned around the zone's centre (the caller draws the panel and the toolbar).
    {
        const float cx = (view.x0 + view.x1) / 2 + nav.panX, cy = (view.y0 + view.y1) / 2 + nav.panY;
        const float hw = (view.x1 - view.x0) / 2 / nav.zoom, hh = (view.y1 - view.y0) / 2 / nav.zoom;
        view = {cx - hw, cy - hh, cx + hw, cy + hh};
    }
    const ImVec2 avail = size;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + avail.x, a.y + avail.y);
    const float vw = view.x1 - view.x0, vh = view.y1 - view.y0;
    const auto [p0, p1] = ui::Fit(a, b, vw, vh);
    const float zoom = (p1.x - p0.x) / vw;
    auto toScreen = [&](float ax, float ay) { return ImVec2(p0.x + (ax - view.x0) * zoom, p0.y + (ay - view.y0) * zoom); };
    const ImVec2 uv0(view.x0 / in.w, view.y0 / in.h), uv1(view.x1 / in.w, view.y1 / in.h);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, p1, true);
    ui::Checker(dl, p0, p1);
    if (guide) dl->AddImage(Tex(guide), p0, p1, uv0, uv1, IM_COL32(255, 255, 255, 80));
    if (composed) dl->AddImage(Tex(composed), p0, p1, uv0, uv1);
    if (shade) dl->AddImage(Tex(shade), p0, p1, uv0, uv1);
    dl->AddRect(p0, p1, ui::kPanelBorder, 0, 0, 2);

    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##cv", ImVec2(p1.x - p0.x, p1.y - p0.y));
    if (paint) {
        PaintOnCanvas(c, *paint, view, p0, zoom, dl);
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemHovered() && io.MouseWheel != 0) {  // wheel: brush size; Ctrl + wheel: zoom
            const float f = io.MouseWheel > 0 ? 1.25f : 1.0f / 1.25f;
            if (io.KeyCtrl) nav.zoom = std::clamp(nav.zoom * f, 1.0f, 16.0f);
            else paint->size = std::clamp(paint->size * f, 1.0f, 64.0f);
        }
        if (ImGui::IsItemHovered() && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
            nav.panX -= io.MouseDelta.x / zoom;
            nav.panY -= io.MouseDelta.y / zoom;
        }
        dl->PopClipRect();
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            ImGui::SetTooltip("%s", Tr("Clic izquierdo: dibujar. Rueda: grosor. Ctrl + rueda: zoom. Botón derecho: desplazar.",
                                       "Left click: draw. Wheel: width. Ctrl + wheel: zoom. Right button: pan."));
        return;
    }
    const ImVec2 m = ImGui::GetIO().MousePos;
    const bool valid = c.selected >= 0 && c.selected < static_cast<int>(c.layers.size());
    Layer* sel = valid ? &c.layers[c.selected] : nullptr;

    // Selection box and its handles (screen space), turned with the layer.
    Frame fr;
    if (sel) fr = FrameOf(*sel);
    auto handleScreen = [&](int k) {
        if (k == kRotateHandle) {  // straight "up" from the middle of the top side
            const ImVec2 top = fr.ToAtlas(fr.w / 2, 0), t = toScreen(top.x, top.y);
            return ImVec2(t.x + fr.s * kKnobGap, t.y - fr.c * kKnobGap);
        }
        const ImVec2 l = HandleLocal(k, fr.w, fr.h), p = fr.ToAtlas(l.x, l.y);
        return toScreen(p.x, p.y);
    };
    auto handleCount = [&] { return !sel ? 0 : sel->isText ? 4 : 9; };  // text: corners only (font size)
    auto handleAt = [&](ImVec2 p) {
        for (int k = handleCount() - 1; k >= 0; --k) {
            const ImVec2 h = handleScreen(k);
            if (std::fabs(p.x - h.x) <= 7 && std::fabs(p.y - h.y) <= 7) return k;
        }
        return -1;
    };

    if (ImGui::IsItemActivated()) {
        g_drag.handle = handleAt(m);
        if (g_drag.handle < 0) {  // pick the top-most layer under the cursor
            const float ax = view.x0 + (m.x - p0.x) / zoom, ay = view.y0 + (m.y - p0.y) / zoom;
            c.selected = -1;
            for (int i = static_cast<int>(c.layers.size()) - 1; i >= 0; --i) {
                const Layer& L = c.layers[i];
                if (!L.visible || L.isBase || L.isPaint) continue;  // base / drawing cover the atlas: not dragged
                const Frame f = FrameOf(L);
                const ImVec2 l = f.ToLocal(ax, ay);
                if (l.x >= 0 && l.y >= 0 && l.x <= f.w && l.y <= f.h) { c.selected = i; break; }
            }
            sel = c.selected >= 0 ? &c.layers[c.selected] : nullptr;
            if (sel) fr = FrameOf(*sel);
        }
        if (sel) g_drag = {g_drag.handle, fr, sel->scale, sel->sx, sel->sy, sel->fontPx};
    }

    if (ImGui::IsItemActive() && sel) {
        const float ax = view.x0 + (m.x - p0.x) / zoom, ay = view.y0 + (m.y - p0.y) / zoom;
        const Frame& f0 = g_drag.f;
        const int k = g_drag.handle;
        if (k < 0) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            sel->x += d.x / zoom;
            sel->y += d.y / zoom;
        } else if (k == kRotateHandle) {  // the angle from the centre to the cursor; Shift: steps of 15
            float deg = std::atan2(ax - f0.cx, -(ay - f0.cy)) * 180.0f / 3.14159265f;
            if (ImGui::GetIO().KeyShift) deg = std::round(deg / 15.0f) * 15.0f;
            sel->rot = std::round(deg * 10.0f) / 10.0f;
        } else {
            // The cursor in the box's own (unturned) coordinates; the opposite corner / side stays put.
            const ImVec2 lc = f0.ToLocal(ax, ay);
            const float r = f0.w, btm = f0.h;
            if (k < 4) {  // corner: same shape, bigger or smaller
                const float fx = (k == 0 || k == 3) ? r - lc.x : lc.x, fy = (k == 0 || k == 1) ? btm - lc.y : lc.y;
                const float f = std::max(0.02f, std::max(fx / std::max(1e-3f, f0.w), fy / std::max(1e-3f, f0.h)));
                if (sel->isText) sel->fontPx = std::clamp(g_drag.fontPx * f, 4.0f, 200.0f);
                else sel->scale = std::clamp(g_drag.scale * f, 0.005f, 20.0f);
            } else if (k == 5 || k == 7) {  // right / left side: wider or narrower only
                const float nw = std::max(1.0f, k == 5 ? lc.x : r - lc.x);
                sel->sx = std::clamp(g_drag.sx * nw / std::max(1e-3f, f0.w), 0.01f, 50.0f);
            } else {  // bottom / top side: taller or shorter only
                const float nh = std::max(1.0f, k == 6 ? lc.y : btm - lc.y);
                sel->sy = std::clamp(g_drag.sy * nh / std::max(1e-3f, f0.h), 0.01f, 50.0f);
            }
            float nw, nh;
            Bounds(*sel, &nw, &nh);
            const float nl = (k == 0 || k == 3 || k == 7) ? r - nw : 0.0f;
            const float nt = (k == 0 || k == 1 || k == 4) ? btm - nh : 0.0f;
            const ImVec2 centre = f0.ToAtlas(nl + nw / 2, nt + nh / 2);
            sel->x = centre.x - nw / 2;
            sel->y = centre.y - nh / 2;
        }
    }

    // Ctrl + wheel (or the wheel with nothing selected): zoom the canvas. Right / middle drag: pan.
    const bool zoomWheel = ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0 && (!sel || ImGui::GetIO().KeyCtrl);
    if (zoomWheel)
        nav.zoom = std::clamp(nav.zoom * (ImGui::GetIO().MouseWheel > 0 ? 1.25f : 1.0f / 1.25f), 1.0f, 16.0f);
    if (ImGui::IsItemHovered() && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        nav.panX -= d.x / zoom;
        nav.panY -= d.y / zoom;
    }
    // Wheel: bigger / smaller around the layer's centre. Arrows: nudge 1 px (Shift: 5).
    if (sel && ImGui::IsItemHovered() && !zoomWheel) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0) {
            float w0, h0;
            Bounds(*sel, &w0, &h0);
            const float f = wheel > 0 ? 1.1f : 1.0f / 1.1f;
            if (sel->isText) sel->fontPx = std::clamp(sel->fontPx * f, 4.0f, 200.0f);
            else sel->scale = std::clamp(sel->scale * f, 0.005f, 20.0f);
            float w1, h1;
            Bounds(*sel, &w1, &h1);
            sel->x -= (w1 - w0) / 2;
            sel->y -= (h1 - h0) / 2;
        }
    }
    if (sel && (ImGui::IsItemHovered() || ImGui::IsItemActive())) {
        const float step = ImGui::GetIO().KeyShift ? 5.0f : 1.0f;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) sel->x -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) sel->x += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) sel->y -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) sel->y += step;
    }

    if (sel) {
        fr = FrameOf(*sel);
        ImVec2 box[4];
        for (int k = 0; k < 4; ++k) box[k] = handleScreen(k);
        dl->AddPolyline(box, 4, ui::kYellow, ImDrawFlags_Closed, 2);
        if (!sel->isText) dl->AddLine(handleScreen(4), handleScreen(kRotateHandle), ui::kYellow, 1.5f);
        const int hover = handleAt(m);
        for (int k = 0; k < handleCount(); ++k) {
            const ImVec2 h = handleScreen(k);
            const bool hot = k == hover || (ImGui::IsItemActive() && k == g_drag.handle);
            if (k == kRotateHandle) {
                dl->AddCircleFilled(h, 6, hot ? ui::kYellow : IM_COL32_WHITE);
                dl->AddCircle(h, 6, IM_COL32(20, 30, 50, 255));
                continue;
            }
            dl->AddRectFilled(ImVec2(h.x - 5, h.y - 5), ImVec2(h.x + 5, h.y + 5), hot ? ui::kYellow : IM_COL32_WHITE);
            dl->AddRect(ImVec2(h.x - 5, h.y - 5), ImVec2(h.x + 5, h.y + 5), IM_COL32(20, 30, 50, 255));
        }
        if (hover >= 0 || (ImGui::IsItemActive() && g_drag.handle >= 0)) {
            const int k = ImGui::IsItemActive() ? g_drag.handle : hover;
            if (k == kRotateHandle) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            } else {  // the resize arrow closest to the turned handle's direction from the centre
                const ImVec2 h = handleScreen(k), o = toScreen(fr.cx, fr.cy);
                const float deg = std::atan2(h.y - o.y, h.x - o.x) * 180.0f / 3.14159265f;
                const int oct = (static_cast<int>(std::lround(deg / 45.0f)) % 4 + 4) % 4;  // 0 E-W, 1 NW-SE, 2 N-S, 3 NE-SW
                ImGui::SetMouseCursor(oct == 0 ? ImGuiMouseCursor_ResizeEW : oct == 1 ? ImGuiMouseCursor_ResizeNWSE
                                      : oct == 2 ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeNESW);
            }
        }
    }
    dl->PopClipRect();
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", Tr("Arrastra para mover. Esquinas: tamaño. Lados: estirar. Rueda: tamaño. Flechas: mover.\n"
                                   "Ctrl + rueda: zoom del molde. Botón derecho: desplazar.",
                                   "Drag to move. Corners: size. Sides: stretch. Wheel: size. Arrows: move.\n"
                                   "Ctrl + wheel: zoom the mould. Right button: pan."));
}

} // namespace ustudio
