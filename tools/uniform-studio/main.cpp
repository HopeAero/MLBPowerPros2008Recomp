// SPDX-License-Identifier: GPL-3.0-or-later
// Uniform Studio: an easy editor, in the look of MLB Power Pros' own menus, for the imported-uniform
// feature of Arrange mode. The player imports PNG/.fsh/.iff art, drags logos and text over the uniform
// mould, and saves UserData/arrange/uniforms/<name>/uniform.ppu (baked by tools/uniform-studio/bake.py).
// No game assets ship with it: the body mould is drawn by the editor (BuildMould), and a precise,
// game-derived region map is only ever loaded from the player's own UserData, never committed.
#include "canvas.h"
#include "config.h"
#include "images.h"
#include "lang.h"
#include "layers.h"
#include "preview.h"
#include "render3d.h"
#include "ui.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#undef LoadImage
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace ustudio;
namespace fs = std::filesystem;
namespace ui = tstudio::ui;
using power_pros_ui::Tr;
using power_pros_ui::Lang;

struct Tex {
    SDL_Texture* t = nullptr;
    int w = 0, h = 0;
};
void Upload(SDL_Renderer* r, Tex& tx, const Image& img) {
    if (img.Empty()) return;
    if (!tx.t || tx.w != static_cast<int>(img.width) || tx.h != static_cast<int>(img.height)) {
        if (tx.t) SDL_DestroyTexture(tx.t);
        tx.t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                 static_cast<int>(img.width), static_cast<int>(img.height));
        SDL_SetTextureBlendMode(tx.t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tx.t, SDL_SCALEMODE_NEAREST);  // crisp pixels when scaled up
        tx.w = static_cast<int>(img.width);
        tx.h = static_cast<int>(img.height);
    }
    SDL_UpdateTexture(tx.t, nullptr, img.rgba.data(), static_cast<int>(img.width * 4));
}

struct DialogResult {
    std::mutex mutex;
    std::string path;
    bool ready = false;
};
void SDLCALL OnDialog(void* userdata, const char* const* files, int) {
    auto* r = static_cast<DialogResult*>(userdata);
    std::lock_guard lock(r->mutex);
    r->path = files && files[0] ? files[0] : "";
    r->ready = true;
}

struct App {
    fs::path userData, config, pyExe, toolDir, tempDir;
    std::array<Canvas, kKindCount> canvases;
    int activeKind = 0;
    std::array<Tex, kKindCount> canvasTex;
    Tex bodyPreviewTex, guideTex, render3dTex;
    bool hasGuide = false;
    std::vector<Vtx> mesh;
    float yaw = 0.6f, pitch = 0.08f, zoom = 1.0f;
    char name[64] = "nuevo";
    std::array<int, 10> backup{};
    bool advanced = false;
    DialogResult dialog;
    bool dialogOpen = false, pendingImport = false, pendingGamePack = false;
    int dialogKind = 0;  // 0 = import art, 1 = load from game pack
    int templateChoice = 0;
    bool dollView = true;  // preview: chibi paper-doll vs flat atlas
    std::string toast;
    bool toastError = false;
};

std::vector<std::string> WideArgs(int argc, char** argv) {
    std::vector<std::string> out;
#if defined(_WIN32)
    int n = 0;
    if (LPWSTR* a = CommandLineToArgvW(GetCommandLineW(), &n)) {
        for (int i = 1; i < n; ++i) out.push_back(tstudio::Utf8(fs::path(a[i])));
        LocalFree(a);
    }
#else
    for (int i = 1; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

void Toast(App& a, const std::string& text, bool error = false) {
    a.toast = text;
    a.toastError = error;
}

Canvas& Active(App& a) { return a.canvases[a.activeKind]; }

void PlaceCentered(const KindInfo& in, Layer& L) {
    float w, h;
    if (L.isText) { w = L.fontPx * 3; h = L.fontPx; }
    else {
        const float fit = std::min({1.0f, in.w / std::max(1.0f, static_cast<float>(L.src.width)),
                                    in.h / std::max(1.0f, static_cast<float>(L.src.height))});
        L.scale = fit;
        w = L.src.width * fit;
        h = L.src.height * fit;
    }
    L.x = (in.w - w) * 0.5f;
    L.y = (in.h - h) * 0.5f;
}

void AddText(App& a) {
    Layer L;
    L.isText = true;
    L.name = Tr("Texto", "Text");
    L.text = "0";
    L.fontPx = a.activeKind == 0 ? 28.0f : 16.0f;
    PlaceCentered(Info(Active(a).kind), L);
    Active(a).layers.push_back(std::move(L));
    Active(a).selected = static_cast<int>(Active(a).layers.size()) - 1;
}

void FinishImport(App& a, const std::string& path) {
    if (path.empty()) return;
    std::vector<Layer> loaded;
    const BridgeResult r = ImportSource(a.pyExe, a.toolDir, a.tempDir, tstudio::PathFromUtf8(path), &loaded);
    if (!r.ok) { Toast(a, r.message, true); return; }
    const KindInfo& in = Info(Active(a).kind);
    for (Layer& L : loaded) { PlaceCentered(in, L); Active(a).layers.push_back(std::move(L)); }
    Active(a).selected = static_cast<int>(Active(a).layers.size()) - 1;
    Toast(a, Tr("Importado. Arrastra las capas sobre el molde.", "Imported. Drag the layers over the mould."));
}

// A single safe path component: keeps [A-Za-z0-9 _-], turns anything else into '_', trims spaces/dots,
// and returns "" for an empty or reserved name. It can never escape the uniforms/ folder.
std::string SanitizeName(const std::string& raw) {
    std::string s;
    for (unsigned char c : raw)
        s += (std::isalnum(c) || c == ' ' || c == '_' || c == '-') ? static_cast<char>(c) : '_';
    const size_t a = s.find_first_not_of(" ."), b = s.find_last_not_of(" .");  // no leading/trailing dot or space
    s = a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    std::string lower;
    for (char c : s) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const char* reserved[] = {"", ".", "..", "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4",
                                     "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4",
                                     "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* r : reserved) if (lower == r) return "";
    return s;
}

// Puts a base layer (template or imported pack) at the bottom, replacing any previous one.
void SetBase(App& a, Layer base) {
    Canvas& c = Active(a);
    base.isBase = true;
    std::erase_if(c.layers, [](const Layer& L) { return L.isBase; });
    base.x = 0;
    base.y = 0;
    base.scale = static_cast<float>(Info(c.kind).w) / std::max(1u, base.src.width);  // fill the atlas
    c.layers.insert(c.layers.begin(), std::move(base));
    c.selected = 0;
}

void ApplyTemplate(App& a, Template t) {
    if (t == Template::None) {
        Canvas& c = Active(a);
        std::erase_if(c.layers, [](const Layer& L) { return L.isBase; });
        c.selected = -1;
        return;
    }
    Image img = BuildTemplate(Active(a).kind, t);
    if (img.Empty()) { Toast(a, Tr("Esta parte no tiene plantilla.", "This part has no template."), true); return; }
    Layer L;
    L.name = Tr("Plantilla", "Template");
    L.src = std::move(img);
    SetBase(a, std::move(L));
}

// Looks for PWM_mdl_main_Common.pack next to the chosen uniform pack (or up a couple of folders).
fs::path FindCommon(const fs::path& pack) {
    fs::path dir = pack.parent_path();
    for (int up = 0; up < 3 && !dir.empty(); ++up, dir = dir.parent_path()) {
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && tstudio::Utf8(e.path().filename()) == "PWM_mdl_main_Common.pack") return e.path();
    }
    return {};
}

void FinishGamePack(App& a, const std::string& path) {
    if (path.empty()) return;
    const fs::path pack = tstudio::PathFromUtf8(path);
    const fs::path common = FindCommon(pack);
    if (common.empty()) {
        Toast(a, Tr("No se encontró PWM_mdl_main_Common.pack junto al pack elegido.",
                    "PWM_mdl_main_Common.pack was not found next to the chosen pack."), true);
        return;
    }
    Layer base;
    const BridgeResult r = LoadGamePack(a.pyExe, a.toolDir, a.tempDir, pack, common, &base);
    if (!r.ok) { Toast(a, r.message, true); return; }
    a.activeKind = 0;  // a pack uniform is the body
    SetBase(a, std::move(base));
    Toast(a, Tr("Uniforme cargado de tu juego. Añade logos/texto encima.",
                "Loaded from your game. Add logos/text on top."));
}

void Save(App& a) {
    const std::string clean = SanitizeName(a.name);
    if (clean.empty()) {
        Toast(a, Tr("Pon un nombre válido (letras, números, espacios, _ o -).",
                    "Enter a valid name (letters, numbers, spaces, _ or -)."), true);
        return;
    }
    const fs::path out = a.userData / "arrange" / "uniforms" / clean / "uniform.ppu";
    const BridgeResult r = SaveUniform(a.pyExe, a.toolDir, a.tempDir, out, a.canvases, a.backup);
    if (r.ok) Toast(a, std::string(Tr("Guardado: ", "Saved: ")) + r.message);
    else Toast(a, r.message, true);
}

// A small generated logo (concentric rings) and a number, so --shot captures a populated editor without
// any game data. Never used in normal runs.
void LoadDemo(App& a) {
    Image img{48, 48, std::vector<uint8_t>(48 * 48 * 4, 0)};
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 48; ++x) {
            const float d = std::hypot(x - 23.5f, y - 23.5f);
            if (d > 22) continue;
            const uint8_t red[4] = {200, 30, 40, 255}, gold[4] = {250, 210, 60, 255};
            std::memcpy(&img.rgba[(y * 48 + x) * 4], (static_cast<int>(d) / 5) % 2 ? red : gold, 4);
        }
    Layer logo;
    logo.name = "demo_logo";
    logo.src = std::move(img);
    PlaceCentered(Info(Active(a).kind), logo);
    logo.y = 18;
    Active(a).layers.push_back(std::move(logo));
    AddText(a);
    Active(a).layers.back().text = "24";
    Active(a).layers.back().y = 70;
    Active(a).selected = 0;
}

void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.FrameRounding = 8;
    s.WindowRounding = 12;
    s.GrabRounding = 8;
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(8, 6);
    ImVec4* c = s.Colors;
    auto f = [](ImU32 v) { return ImGui::ColorConvertU32ToFloat4(v); };
    c[ImGuiCol_WindowBg] = f(ui::kWindow);
    c[ImGuiCol_PopupBg] = f(ui::kPanel);
    c[ImGuiCol_Border] = f(ui::kPanelBorder);
    c[ImGuiCol_FrameBg] = f(power_pros_ui::Rgba(12, 20, 34));
    c[ImGuiCol_FrameBgHovered] = f(ui::kPanelHeader);
    c[ImGuiCol_Header] = f(ui::kBlueBottom);
    c[ImGuiCol_HeaderHovered] = f(ui::kPanelHeader);
    c[ImGuiCol_Text] = f(IM_COL32_WHITE);
    c[ImGuiCol_CheckMark] = f(ui::kYellow);
}

void DrawBand(App& a, float w) {
    using namespace power_pros_ui;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = 64;
    const int start = dl->VtxBuffer.Size;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32_WHITE);
    Shade(dl, start, 0, h, ui::kBandTop, ui::kBandBottom);
    dl->AddLine(ImVec2(0, h), ImVec2(w, h), Rgba(255, 255, 255, 140), 3);
    ShadowedText(dl, g_title, 30, ImVec2(18, 6), IM_COL32_WHITE, "POWER PROS WII 2008");
    Text(dl, g_bold, 15, ImVec2(20, 41), ui::kYellow, "U N I F O R M   S T U D I O");
    ImGui::SetCursorScreenPos(ImVec2(w - 100, 14));
    Lang lang = g_lang;
    if (ui::LangSwitch(&lang)) {
        g_lang = lang;
        if (fs::exists(a.config))
            tstudio::WriteSetting(a.config, "ui", "language", lang == Lang::En ? "\"en\"" : "\"es\"");
    }
}

void DrawLeft(App& a, float h) {
    if (!ui::BeginPanel("##left", Tr("Capas", "Layers"), ImVec2(300, h))) { ui::EndPanel(); return; }

    // Atlas slot picker.
    static const power_pros_ui::Phrase kinds[kKindCount] = {{"Cuerpo", "Body"}, {"Gorra", "Cap"},
                                                            {"Casco", "Helmet"}, {"Número", "Number"}};
    ImGui::TextUnformatted(Tr("Parte del uniforme", "Uniform part"));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##kind", kinds[a.activeKind].get())) {
        for (int i = 0; i < kKindCount; ++i)
            if (ImGui::Selectable(kinds[i].get(), i == a.activeKind)) a.activeKind = i;
        ImGui::EndCombo();
    }
    ImGui::Dummy(ImVec2(0, 4));

    // Starter template (original art) and "load from my game" base.
    static const power_pros_ui::Phrase tpl[] = {{"Plantilla: ninguna", "Template: none"}, {"Blanco liso", "Plain white"},
                                                {"Pinstripes", "Pinstripes"}, {"Dos tonos", "Two-tone"},
                                                {"Franja/placket", "Placket stripe"}};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##tpl", tpl[a.templateChoice].get())) {
        for (int i = 0; i < 5; ++i)
            if (ImGui::Selectable(tpl[i].get(), i == a.templateChoice)) {
                a.templateChoice = i;
                ApplyTemplate(a, static_cast<Template>(i));
            }
        ImGui::EndCombo();
    }
    ImGui::Dummy(ImVec2(0, 2));
    if (ui::Pill(Tr("Cargar de mi juego...", "Load from my game..."), ui::kGreyTop, ui::kGreyBottom, true))
        a.pendingGamePack = true;
    ImGui::Dummy(ImVec2(0, 4));
    if (ui::Pill(Tr("Importar", "Import"), ui::kBlueTop, ui::kBlueBottom, true)) a.pendingImport = true;
    ImGui::SameLine();
    if (ui::Pill(Tr("+ Texto", "+ Text"), ui::kGreenTop, ui::kGreenBottom, true)) AddText(a);
    ImGui::Dummy(ImVec2(0, 4));

    Canvas& c = Active(a);
    for (int i = 0; i < static_cast<int>(c.layers.size()); ++i) {
        Layer& L = c.layers[i];
        ImGui::PushID(i);
        bool vis = L.visible;
        if (ImGui::Checkbox("##vis", &vis)) L.visible = vis;
        ImGui::SameLine();
        std::string label = (L.isText ? std::string("\"") + L.text + "\"" : L.name);
        if (label.size() > 16) label = label.substr(0, 15) + "..";
        if (ImGui::Selectable(label.c_str(), c.selected == i, 0, ImVec2(120, 0))) c.selected = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("^") && i > 0) { std::swap(c.layers[i], c.layers[i - 1]); c.selected = i - 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("v") && i + 1 < static_cast<int>(c.layers.size())) { std::swap(c.layers[i], c.layers[i + 1]); c.selected = i + 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) { c.layers.erase(c.layers.begin() + i); c.selected = -1; ImGui::PopID(); break; }
        ImGui::PopID();
    }

    // Selected-layer properties.
    if (c.selected >= 0 && c.selected < static_cast<int>(c.layers.size())) {
        Layer& L = c.layers[c.selected];
        ImGui::SeparatorText(Tr("Capa seleccionada", "Selected layer"));
        if (L.isText) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s", L.text.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText(Tr("Texto##t", "Text##t"), buf, sizeof buf)) L.text = buf;
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat(Tr("Tamaño##f", "Size##f"), &L.fontPx, 6.0f, 80.0f, "%.0f");
            float col[3] = {L.color[0] / 255.0f, L.color[1] / 255.0f, L.color[2] / 255.0f};
            if (ImGui::ColorEdit3(Tr("Color##c", "Colour##c"), col, ImGuiColorEditFlags_NoInputs)) {
                L.color[0] = static_cast<unsigned char>(col[0] * 255);
                L.color[1] = static_cast<unsigned char>(col[1] * 255);
                L.color[2] = static_cast<unsigned char>(col[2] * 255);
            }
        } else {
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat(Tr("Escala##s", "Scale##s"), &L.scale, 0.1f, 4.0f, "%.2f");
        }
        ImGui::Text("X %.0f  Y %.0f", L.x, L.y);
    }
    ui::EndPanel();
}

void DrawRightBottom(App& a, ImVec2 pos, ImVec2 size) {
    ImGui::SetCursorPos(pos);
    if (!ui::BeginPanel("##save", Tr("Uniforme", "Uniform"), size)) { ui::EndPanel(); return; }
    ImGui::TextUnformatted(Tr("Nombre", "Name"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##name", a.name, sizeof a.name);
    ImGui::Checkbox(Tr("Colores de respaldo (avanzado)", "Backup colours (advanced)"), &a.advanced);
    if (a.advanced) {
        for (int i = 0; i < 10; ++i) {
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(110);
            ImGui::SliderInt("##bk", &a.backup[i], 0, 31);
            ImGui::PopID();
            if (i % 2 == 0) ImGui::SameLine();
        }
    }
    ImGui::Dummy(ImVec2(0, 6));
    if (ui::Pill(Tr("Guardar uniforme", "Save uniform"), ui::kOrangeTop, ui::kOrangeBottom)) Save(a);
    ui::EndPanel();
}

int Run(const std::vector<std::string>& args) {
#if defined(_WIN32)
    SetProcessDPIAware();
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window* window = SDL_CreateWindow("Power Pros Wii 2008 Uniform Studio", 1200, 780,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_SetWindowMinimumSize(window, 1000, 680);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    power_pros_ui::LoadFonts();
    ImGui::GetIO().FontDefault = power_pros_ui::g_menu;
    ApplyStyle();

    auto app = std::make_unique<App>();
    App& a = *app;
    const fs::path base = tstudio::PathFromUtf8(SDL_GetBasePath());
    a.userData = base / "UserData";
    a.toolDir = base;
    a.pyExe = "python";
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--userdata") a.userData = tstudio::PathFromUtf8(args[i + 1]);
        else if (args[i] == "--tooldir") a.toolDir = tstudio::PathFromUtf8(args[i + 1]);
        else if (args[i] == "--python") a.pyExe = tstudio::PathFromUtf8(args[i + 1]);
        else if (args[i] == "--name") std::snprintf(a.name, sizeof a.name, "%s", args[i + 1].c_str());
        else if (args[i] == "--yaw") a.yaw = static_cast<float>(std::atof(args[i + 1].c_str()));
        else if (args[i] == "--pitch") a.pitch = static_cast<float>(std::atof(args[i + 1].c_str()));
    }
    const fs::path pyenv = "C:/Users/david/.pyenv/pyenv-win/versions/3.12.0/python.exe";
    if (a.pyExe == "python" && fs::exists(pyenv)) a.pyExe = pyenv;  // dev default; Setup passes --python
    a.config = a.userData / "Config.toml";
    a.tempDir = a.userData / "Cache" / "uniform_studio";
    for (int i = 0; i < kKindCount; ++i) a.canvases[i].kind = static_cast<Kind>(i);
    a.mesh = BuildMannequin();

    {
        const auto v = fs::exists(a.config) ? tstudio::ReadSetting(a.config, "ui", "language") : std::nullopt;
        power_pros_ui::g_lang = power_pros_ui::LangFromSetting(v.value_or("auto"));
    }
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--lang") power_pros_ui::g_lang = power_pros_ui::LangFromSetting(args[i + 1]);

    // The body mould guide. An authored schematic is the default; a precise map generated from the player's
    // own install (never shipped) overrides it if present. Neither is game data committed to the repo.
    {
        std::string err;
        const fs::path precise = a.userData / "arrange" / "uniform" / "body_regions.png";
        if (auto img = tstudio::LoadImage(precise, &err)) Upload(renderer, a.guideTex, *img);
        else Upload(renderer, a.guideTex, BuildMould(Kind::Body));
        a.hasGuide = true;
    }
    for (const auto& arg : args)
        if (arg == "--demo") { ApplyTemplate(a, Template::Pinstripes); LoadDemo(a); }

    // Headless dump of the composed body atlas, so test_roundtrip.py can verify Compose/RasterText.
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--compose-test") {
            if (Active(a).layers.empty()) LoadDemo(a);
            const Image img = Compose(a.canvases[0]);
            std::string err;
            const bool ok = tstudio::SavePng(tstudio::PathFromUtf8(args[i + 1]), img, &err);
            std::fprintf(ok ? stdout : stderr, "%s\n", ok ? "COMPOSED" : err.c_str());
            return ok ? 0 : 1;
        }

    // Headless end-to-end check of the C++ -> Python save bridge (used by the build/test step).
    for (const auto& arg : args)
        if (arg == "--save-test") {
            if (Active(a).layers.empty()) LoadDemo(a);
            const std::string clean = SanitizeName(a.name);
            const BridgeResult r = SaveUniform(a.pyExe, a.toolDir, a.tempDir,
                                               a.userData / "arrange" / "uniforms" / (clean.empty() ? "nuevo" : clean) / "uniform.ppu",
                                               a.canvases, a.backup);
            std::fprintf(r.ok ? stdout : stderr, "%s: %s\n", r.ok ? "SAVED" : "ERROR", r.message.c_str());
            return r.ok ? 0 : 1;
        }

    int shotFrames = -1;
    fs::path shotPath;
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--shot") { shotPath = tstudio::PathFromUtf8(args[i + 1]); shotFrames = 6; }

    bool quit = false;
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) quit = true;
        }
        {
            std::lock_guard lock(a.dialog.mutex);
            if (a.dialog.ready) {
                a.dialog.ready = false;
                a.dialogOpen = false;
                if (a.dialogKind == 1) FinishGamePack(a, a.dialog.path);
                else FinishImport(a, a.dialog.path);
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Recompose the active atlas (for its canvas) and the body atlas (for the preview).
        const Image activeImg = Compose(Active(a));
        Upload(renderer, a.canvasTex[a.activeKind], activeImg);
        const Image bodyImg = Compose(a.canvases[0]);
        Upload(renderer, a.bodyPreviewTex, bodyImg);
        const int colors = CountColors(bodyImg);
        if (a.dollView) Upload(renderer, a.render3dTex, RenderMesh(a.mesh, bodyImg, 440, 440, a.yaw, a.pitch, a.zoom));

        const ImVec2 vs = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(vs);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        DrawBand(a, vs.x);

        const float top = 76, footer = 44, gap = 10;
        const float bodyH = vs.y - top - footer - gap;
        const float rightW = 360, leftW = 300;
        ImGui::SetCursorPos(ImVec2(10, top));
        DrawLeft(a, bodyH);

        const float midX = 10 + leftW + gap;
        const float midW = vs.x - leftW - rightW - 3 * gap - 10;
        ImGui::SetCursorPos(ImVec2(midX, top));
        DrawCanvas(Active(a), a.canvasTex[a.activeKind].t, a.hasGuide && a.activeKind == 0 ? a.guideTex.t : nullptr,
                   ImVec2(midW, bodyH));

        const float rightX = vs.x - rightW - 10;
        const float previewH = bodyH * 0.55f;
        ImGui::SetCursorPos(ImVec2(rightX, top));
        DrawPreview(a.render3dTex.t, a.bodyPreviewTex.t, a.hasGuide ? a.guideTex.t : nullptr,
                    ImVec2(rightW, previewH), colors, &a.dollView, &a.yaw, &a.pitch, &a.zoom);
        DrawRightBottom(a, ImVec2(rightX, top + previewH + gap), ImVec2(rightW, bodyH - previewH - gap));

        if (!a.toast.empty())
            power_pros_ui::ShadowedText(ImGui::GetWindowDrawList(), power_pros_ui::g_bold, 16,
                                        ImVec2(16, vs.y - footer + 10), a.toastError ? ui::kRedTop : ui::kMuted,
                                        a.toast.c_str());
        ImGui::End();

        if ((a.pendingImport || a.pendingGamePack) && !a.dialogOpen) {
            const bool pack = a.pendingGamePack;
            a.pendingImport = a.pendingGamePack = false;
            a.dialogOpen = true;
            a.dialogKind = pack ? 1 : 0;
            static const SDL_DialogFileFilter art[] = {{"PNG / DDS / FSH / IFF", "png;dds;fsh;iff"}};
            static const SDL_DialogFileFilter packs[] = {{"PWM_mdl_uniform_*.pack", "pack"}};
            SDL_ShowOpenFileDialog(OnDialog, &a.dialog, window, pack ? packs : art, 1, nullptr, false);
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 16, 24, 38, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        if (shotFrames > 0 && --shotFrames == 0) {
            if (SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr)) {
                std::error_code ec;
                fs::create_directories(shotPath.parent_path(), ec);
                SDL_SavePNG(s, tstudio::Utf8(shotPath).c_str());
                SDL_DestroySurface(s);
            }
            quit = true;
        }
    }

    for (auto& tx : a.canvasTex) if (tx.t) SDL_DestroyTexture(tx.t);
    if (a.bodyPreviewTex.t) SDL_DestroyTexture(a.bodyPreviewTex.t);
    if (a.render3dTex.t) SDL_DestroyTexture(a.render3dTex.t);
    if (a.guideTex.t) SDL_DestroyTexture(a.guideTex.t);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
} // namespace

int main(int argc, char** argv) { return Run(WideArgs(argc, argv)); }
