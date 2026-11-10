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
#include "paint.h"
#include "pieces.h"
#include "project.h"
#include "zones.h"
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
#include <fstream>
#include <sstream>
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

// The editable state, for undo / redo.
struct Snapshot {
    std::array<Canvas, kKindCount> canvases;
    Pieces pieces;
    bool piecesOn = false;
    std::array<int, 10> backup{};
};

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
    GameBase gameBase;      // the pieces of an Arrange uniform, from the player's own game (pieces.h)
    Pieces pieces;
    bool piecesOn = false;  // the body base layer is the piece-coloured atlas
    int pickingPiece = -1;  // the piece whose colour grid is open
    GameDoll doll;          // the game's chibi figure for the preview (render3d.h), from the player's disc
    std::vector<Zone> zones;  // chest, back, sleeves... (zones.h); zones[zone] is the one being edited
    int zone = 0;
    Tex shadeTex;           // darkens the atlas outside the zone
    CanvasNav nav;          // zoom / pan of the mould
    bool inspect = false;   // the big figure viewer is open
    Tex inspectTex;
    float iyaw = 0.0f, ipitch = 0.08f, izoom = 1.0f, ipanX = 0.0f, icenterY = 4.0f;
    int shadeZone = -2;     // the zone shadeTex was made for
    std::string toast;
    bool toastError = false;
    int leftTab = 0;        // 0 colours, 1 logos and text, 2 number, 3 drawing
    PaintTool paint;        // the drawing tab's tool, size and colour (canvas.h)
    bool clipZone = true;   // drawing stays inside the zone being edited
    int pattern = 0;        // paint.h Pattern
    float patWidth = 2.0f, patSpacing = 8.0f, patOffset = 0.0f;
    bool patPreview = false;  // the pattern shown on the mould before it is applied
    Image activeImg;        // the active part composed this frame (the picker and the fill read it)
    std::vector<Snapshot> undo, redo;
    Snapshot stable;        // the last settled state (what an undo goes back past)
    uint64_t stableKey = 0, savedKey = 0;
    bool stableInit = false;
    bool showOpen = false, confirmNew = false;
    std::vector<SavedUniform> openList;
    std::vector<Tex> openThumbs;  // textures of openList's thumbnails
    bool openThumbsDirty = false;
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

// Fit a new layer inside the zone being edited (60 % of it) and centre it there.
void PlaceInZone(const App& a, Layer& L) {
    const KindInfo& in = Info(Active(const_cast<App&>(a)).kind);
    float zx0 = 0, zy0 = 0, zx1 = static_cast<float>(in.w), zy1 = static_cast<float>(in.h);
    if (a.zone >= 0 && a.zone < static_cast<int>(a.zones.size()) && a.zones[a.zone].kind == Active(const_cast<App&>(a)).kind) {
        const Zone& z = a.zones[a.zone];
        zx0 = z.x0; zy0 = z.y0; zx1 = z.x1; zy1 = z.y1;
    }
    const float zw = zx1 - zx0, zh = zy1 - zy0;
    float w, h;
    if (L.isText) {
        L.fontPx = std::clamp(zh * 0.5f, 6.0f, 80.0f);
        w = L.fontPx * 0.55f * std::max<size_t>(1, L.text.size());
        h = L.fontPx;
    } else {
        L.sx = L.sy = 1.0f;
        L.scale = std::min({zw * 0.6f / std::max(1.0f, static_cast<float>(L.src.width)),
                            zh * 0.6f / std::max(1.0f, static_cast<float>(L.src.height))});
        w = L.DrawnW();
        h = L.DrawnH();
    }
    L.x = zx0 + (zw - w) * 0.5f;
    L.y = zy0 + (zh - h) * 0.5f;
}

// Centre a layer in the zone being edited (horizontally, vertically or both), keeping its size.
void CenterInZone(const App& a, Layer& L, bool horizontal, bool vertical) {
    const KindInfo& in = Info(Active(const_cast<App&>(a)).kind);
    float zx0 = 0, zy0 = 0, zx1 = static_cast<float>(in.w), zy1 = static_cast<float>(in.h);
    if (a.zone >= 0 && a.zone < static_cast<int>(a.zones.size()) && a.zones[a.zone].kind == Active(const_cast<App&>(a)).kind) {
        const Zone& z = a.zones[a.zone];
        zx0 = z.x0; zy0 = z.y0; zx1 = z.x1; zy1 = z.y1;
    }
    const float w = L.isText ? L.fontPx * 0.55f * std::max<size_t>(1, L.text.size()) : L.DrawnW();
    const float h = L.isText ? L.fontPx : L.DrawnH();
    if (horizontal) L.x = zx0 + (zx1 - zx0 - w) * 0.5f;
    if (vertical) L.y = zy0 + (zy1 - zy0 - h) * 0.5f;
}

void AddText(App& a) {
    Layer L;
    L.isText = true;
    L.name = Tr("Texto", "Text");
    L.text = "0";
    PlaceInZone(a, L);
    Active(a).layers.push_back(std::move(L));
    Active(a).selected = static_cast<int>(Active(a).layers.size()) - 1;
}

void FinishImport(App& a, const std::string& path) {
    if (path.empty()) return;
    std::vector<Layer> loaded;
    const BridgeResult r = ImportSource(a.pyExe, a.toolDir, a.tempDir, tstudio::PathFromUtf8(path), &loaded);
    if (!r.ok) { Toast(a, r.message, true); return; }
    for (Layer& L : loaded) { PlaceInZone(a, L); Active(a).layers.push_back(std::move(L)); }
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
    c.selected = -1;  // the base fills the atlas: nothing to move or resize
}

void ApplyTemplate(App& a, Template t) {
    if (a.activeKind == 0) a.piecesOn = false;
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


// --- Colours per piece, as the game's Arrange editor ------------------------------------------------
// The pieces come from the player's own game (studio_bridge.py "gamebase" -> a local cache under
// UserData), found through Config.toml [paths] dvd_root like the Texture Studio. Nothing is shipped.
fs::path GameBaseCache(const App& a) { return a.userData / "arrange" / "uniform" / "game_base.bin"; }
fs::path GameDollCache(const App& a) { return a.userData / "arrange" / "uniform" / "game_doll.bin"; }

// The player's project.bin, from Config.toml [paths] dvd_root (as the Texture Studio), or empty.
fs::path FindProjectBin(const App& a) {
    const auto dvd = fs::exists(a.config) ? tstudio::ReadSetting(a.config, "paths", "dvd_root") : std::nullopt;
    if (!dvd) return {};
    fs::path root = tstudio::PathFromUtf8(*dvd);
    if (root.is_relative()) root = tstudio::PathFromUtf8(SDL_GetBasePath()) / root;
    const fs::path project = root / "files" / "project.bin";
    return fs::exists(project) ? project : fs::path{};
}

// The game's figure for the preview: from the local cache, or read once from the player's disc. Without
// the game the editor keeps its own mannequin (no message: the editor works without it).
void EnsureGameDoll(App& a) {
    if (a.doll.ok || LoadGameDoll(GameDollCache(a), &a.doll)) return;
    const fs::path project = FindProjectBin(a);
    if (project.empty()) return;
    if (ExtractFromGame(a.pyExe, a.toolDir, a.tempDir, "gamedoll", project, GameDollCache(a)).ok)
        LoadGameDoll(GameDollCache(a), &a.doll);
}

bool EnsureGameBase(App& a) {
    if (a.gameBase.ok) return true;
    if (LoadGameBase(GameBaseCache(a), &a.gameBase)) {
        a.zones = BuildZones(&a.gameBase, &a.doll);
        return true;
    }
    const fs::path project = FindProjectBin(a);
    if (project.empty()) {
        Toast(a, Tr("No se encontró el juego: instálalo con el Setup y abre el editor desde el launcher.",
                    "The game was not found: install it with the Setup and open the editor from the launcher."), true);
        return false;
    }
    const BridgeResult r = ExtractFromGame(a.pyExe, a.toolDir, a.tempDir, "gamebase", project, GameBaseCache(a));
    const bool loaded = r.ok && LoadGameBase(GameBaseCache(a), &a.gameBase);
    if (loaded) {
        a.zones = BuildZones(&a.gameBase, &a.doll);
        a.zone = 0;
        a.shadeZone = -2;
    }
    if (!loaded) {
        Toast(a, r.ok ? Tr("No se pudieron leer las piezas del juego.", "Could not read the game's pieces.") : r.message,
              true);
        return false;
    }
    return true;
}

// The cap colour behind the cap / helmet logo (layers.h FillBehind): the CAP piece when colouring by piece,
// else the average of the cap crown in the composed body atlas, else white.
std::array<uint8_t, 3> CapFill(const App& a, const Image& body) {
    if (a.piecesOn && a.gameBase.ok) {
        const uint32_t c = PieceRgb(a.gameBase, a.pieces, kCap, 0);
        return {static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c)};
    }
    for (const Zone& z : a.zones) {
        if (z.kind != Kind::Body || z.mask.empty() || std::string(z.en) != "Cap") continue;
        uint64_t s[3] = {}, n = 0;
        for (size_t i = 0; i < z.mask.size() && i * 4 + 3 < body.rgba.size(); ++i)
            if (z.mask[i] && body.rgba[i * 4 + 3] >= 128) {
                for (int k = 0; k < 3; ++k) s[k] += body.rgba[i * 4 + k];
                ++n;
            }
        if (n) return {static_cast<uint8_t>(s[0] / n), static_cast<uint8_t>(s[1] / n), static_cast<uint8_t>(s[2] / n)};
    }
    return {255, 255, 255};
}

// Paint the body base with the pieces and keep the backup colours (the record bytes the game falls
// back to) in step: byte 1 upper, 2 lower, 3 line, 4 line type, 5 cap, 6 brim, 7 sleeves.
void ApplyPieces(App& a) {
    const int keep = a.activeKind;
    a.activeKind = 0;
    Layer L;
    L.name = Tr("Piezas", "Pieces");
    L.src = BuildPiecesBase(a.gameBase, a.pieces);
    SetBase(a, std::move(L));
    a.activeKind = keep;
    a.piecesOn = true;
    // A free colour falls back to the nearest of the game's 32 (the record bytes are colour numbers).
    auto num = [&](int p) { return a.pieces.rgb[p] >= 0 ? NearestColor(a.gameBase, a.pieces.rgb[p]) : a.pieces.color[p]; };
    a.backup[1] = num(kUpper);
    a.backup[2] = num(kLower);
    a.backup[3] = num(kLine);
    a.backup[4] = a.pieces.lineType;
    a.backup[5] = num(kCap);
    a.backup[6] = num(kBrim);
    a.backup[7] = num(kSleeves);
    a.backup[8] = a.pieces.number;
    a.backup[9] = a.pieces.outline;
}

ImVec4 Rgb4(uint32_t c) { return ImVec4(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, 1.0f); }
ImVec4 Swatch(const GameBase& b, int color, int shade) { return Rgb4(ColorRgb(b, color, shade)); }

bool Tab(const char* label, bool on);

// A row with a piece's colour: the whole row opens the game's 32 colours (8 columns x 4 rows, number =
// column x 4 + row, shown as COLOR NO. n + 1 as in the game) and, with `rgb`, a free colour picker (any
// RGB; -1 = the game colour). Returns true when the colour changed.
bool ColourRow(App& a, const char* id, const char* label, int* value, bool enabled = true, int* rgb = nullptr) {
    bool changed = false;
    ImGui::PushID(id);
    if (!enabled) ImGui::BeginDisabled();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool forced = rgb && a.pickingPiece >= 0 && rgb == &a.pieces.rgb[static_cast<size_t>(a.pickingPiece)];
    if (ImGui::Selectable("##row", false, 0, ImVec2(w, 30)) || forced) ImGui::OpenPopup("##grid");
    if (forced) a.pickingPiece = -1;  // --pick-piece (screenshots)
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool free = rgb && *rgb >= 0;
    const ImVec4 c = free ? Rgb4(static_cast<uint32_t>(*rgb)) : Swatch(a.gameBase, *value, 0);
    dl->AddRectFilled(ImVec2(at.x + 4, at.y + 3), ImVec2(at.x + 52, at.y + 27), ImGui::ColorConvertFloat4ToU32(c), 6);
    dl->AddRect(ImVec2(at.x + 4, at.y + 3), ImVec2(at.x + 52, at.y + 27), IM_COL32(255, 255, 255, 120), 6);
    dl->AddText(ImVec2(at.x + 64, at.y + 6), ImGui::GetColorU32(ImGuiCol_Text), label);
    char num[16];
    if (free) std::snprintf(num, sizeof num, "#%06X", *rgb);
    else std::snprintf(num, sizeof num, "%d", *value + 1);
    dl->AddText(ImVec2(at.x + w - ImGui::CalcTextSize(num).x - 10, at.y + 6), ImGui::GetColorU32(ImGuiCol_TextDisabled), num);
    if (!enabled) ImGui::EndDisabled();
    if (ImGui::BeginPopup("##grid")) {
        ImGui::TextUnformatted(label);
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 8; ++col) {
                const int n = col * 4 + row;
                ImGui::PushID(n);
                if (col) ImGui::SameLine(0, 4);
                if (ImGui::ColorButton("##c", Swatch(a.gameBase, n, 0), ImGuiColorEditFlags_NoTooltip, ImVec2(30, 30))) {
                    *value = n;
                    if (rgb) *rgb = -1;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (n == *value && !free)
                    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ui::kYellow, 4, 0, 3);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("COLOR NO. %d", n + 1);
                ImGui::PopID();
            }
        if (rgb) {  // any colour: the body atlas is an image in the .ppu, so the game shows it as is
            ImGui::SeparatorText(Tr("Color libre (cualquier RGB)", "Free colour (any RGB)"));
            const uint32_t now = free ? static_cast<uint32_t>(*rgb) : ColorRgb(a.gameBase, *value, 0);
            float f[3] = {((now >> 16) & 255) / 255.0f, ((now >> 8) & 255) / 255.0f, (now & 255) / 255.0f};
            ImGui::SetNextItemWidth(8 * 34 - 4);
            if (ImGui::ColorPicker3("##free", f, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayRGB |
                                                     ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueBar)) {
                *rgb = static_cast<int>(std::lround(f[0] * 255)) << 16 | static_cast<int>(std::lround(f[1] * 255)) << 8 |
                       static_cast<int>(std::lround(f[2] * 255));
                changed = true;
            }
            if (free) ImGui::TextDisabled(Tr("Respaldo en el juego: COLOR NO. %d", "Game backup: COLOR NO. %d"),
                                          NearestColor(a.gameBase, static_cast<uint32_t>(*rgb)) + 1);
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

// part 0: the pieces' colours and the line type; part 1: the number (colours, sample, where it goes).
void DrawPieces(App& a, int part) {
    if (!a.piecesOn) return;
    bool changed = false;
    if (part == 0) {
        ImGui::TextDisabled("%s", Tr("Haz clic en una pieza para elegir su color.", "Click a piece to pick its colour."));
        static const power_pros_ui::Phrase names[kPieceCount] = {
            {"GORRA", "CAP"}, {"VISERA", "BRIM"}, {"MANGAS / ETC", "SLEEVES / ETC"},
            {"CAMISETA", "UPPER"}, {"PANTALÓN", "LOWER"}, {"LÍNEA", "LINE"}};
        for (int p = 0; p < kPieceCount; ++p) {
            // As the game's editor (0x80CEE534): no line -> LINE off; pinstripes -> LOWER off (the trousers
            // take the jersey colour and the stripes, 0x806BDE24).
            const bool off = (p == kLine && a.pieces.lineType == 0) || (p == kLower && a.pieces.lineType == 2);
            char id[8];
            std::snprintf(id, sizeof id, "p%d", p);
            changed |= ColourRow(a, id, names[p].get(), &a.pieces.color[p], !off, &a.pieces.rgb[p]);
        }
        ImGui::SeparatorText(Tr("Tipo de línea", "Line type"));
        static const power_pros_ui::Phrase types[3] = {{"Sin línea", "No line"}, {"Ribete", "Piping"},
                                                       {"Rayas", "Pinstripes"}};
        for (int t = 0; t < 3; ++t) {
            if (t) ImGui::SameLine();
            if (Tab(types[t].get(), a.pieces.lineType == t)) {
                a.pieces.lineType = t;
                changed = true;
            }
        }
        if (a.pieces.lineType == 2)
            ImGui::TextDisabled("%s", Tr("Con rayas, el pantalón lleva el color de la camiseta (como en el juego).",
                                         "With pinstripes the trousers take the jersey colour (as in the game)."));
    }
    if (part == 1) {
        // The number, as the game's UNIFORM # / # OUTLINE: its colours go with the uniform (the game takes
        // them when the uniform is picked), and a sample number shows on the figure.
        ImGui::SeparatorText(Tr("Colores", "Colours"));
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", Tr("El número lo pinta el juego con su paleta: solo admite sus 32 colores.",
                                     "The game paints the number with its palette: only its 32 colours."));
        ImGui::PopTextWrapPos();
        changed |= ColourRow(a, "num", Tr("NÚMERO", "UNIFORM #"), &a.pieces.number);
        changed |= ColourRow(a, "out", Tr("BORDE DEL NÚMERO", "# OUTLINE"), &a.pieces.outline);
        ImGui::SeparatorText(Tr("Dorsal de prueba (solo vista previa)", "Sample number (preview only)"));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##pn", &a.pieces.previewNumber, 0, 99);
        // Where the number goes (the game applies it: chest positions are its own, the moves shift its tables).
        ImGui::SeparatorText(Tr("Número del pecho", "Chest number"));
        static const power_pros_ui::Phrase chestNames[10] = {
            {"Como el juego", "As the game"}, {"Sin número", "No number"},
            {"Posición 1", "Position 1"}, {"Posición 2", "Position 2"}, {"Posición 3", "Position 3"},
            {"Posición 4", "Position 4"}, {"Posición 5", "Position 5"}, {"Posición 6", "Position 6"},
            {"Posición 7", "Position 7"}, {"Posición 8", "Position 8"}};
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##chest", chestNames[std::clamp(a.pieces.chest, 0, 9)].get())) {
            for (int i = 0; i < 10; ++i)
                if (ImGui::Selectable(chestNames[i].get(), i == a.pieces.chest)) a.pieces.chest = i;
            ImGui::EndCombo();
        }
        const float half = (ImGui::GetContentRegionAvail().x - 8) / 2;
        if (a.pieces.chest != 1) {
            ImGui::SetNextItemWidth(half);
            ImGui::SliderInt("##cdx", &a.pieces.chestDx, -40, 40, Tr("mover X %d", "move X %d"));
            ImGui::SameLine(0, 8);
            ImGui::SetNextItemWidth(half);
            ImGui::SliderInt("##cdy", &a.pieces.chestDy, -40, 40, Tr("mover Y %d", "move Y %d"));
        }
        ImGui::SeparatorText(Tr("Número de la espalda", "Back number"));
        ImGui::SetNextItemWidth(half);
        ImGui::SliderInt("##bdx", &a.pieces.backDx, -40, 40, Tr("mover X %d", "move X %d"));
        ImGui::SameLine(0, 8);
        ImGui::SetNextItemWidth(half);
        ImGui::SliderInt("##bdy", &a.pieces.backDy, -40, 40, Tr("mover Y %d", "move Y %d"));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##bs", &a.pieces.backSize, 40, 160, Tr("tamaño %d%%", "size %d%%"));
        ImGui::Dummy(ImVec2(0, 6));
        if (ui::Pill(Tr("Número como el juego", "Number as the game"), ui::kGreyTop, ui::kGreyBottom, true)) {
            a.pieces.chest = a.pieces.chestDx = a.pieces.chestDy = a.pieces.backDx = a.pieces.backDy = 0;
            a.pieces.backSize = 100;
        }
        ImGui::SameLine();
        if (ui::Pill(Tr("Ver la espalda", "See the back"), ui::kBlueTop, ui::kBlueBottom, true)) {
            a.inspect = true;
            a.iyaw = 3.14159f;
            a.ipitch = 0.08f;
        }
    }
    if (changed) ApplyPieces(a);
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
    a.piecesOn = false;
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
    const auto capFill = CapFill(a, Compose(a.canvases[0]));
    const std::array<int, 6> layout{a.pieces.chest, a.pieces.chestDx, a.pieces.chestDy, a.pieces.backDx, a.pieces.backDy,
                                    a.pieces.backSize};
    const BridgeResult r = SaveUniform(a.pyExe, a.toolDir, a.tempDir, out, a.canvases, a.backup, &a.doll, &capFill, a.piecesOn ? 1 : 0,
                                 a.piecesOn && a.pieces.LayoutSet() ? &layout : nullptr);
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

// ---- Undo / redo --------------------------------------------------------------------------------------
// The editable state as a snapshot. A change is recorded when the player stops interacting (no item
// active, no mouse button down), so a whole drag or slider move is one step.
Snapshot Snap(const App& a) { return {a.canvases, a.pieces, a.piecesOn, a.backup}; }

void Restore(App& a, const Snapshot& s) {
    a.canvases = s.canvases;
    a.pieces = s.pieces;
    a.piecesOn = s.piecesOn;
    a.backup = s.backup;
    for (auto& c : a.canvases)
        if (c.selected >= static_cast<int>(c.layers.size())) c.selected = -1;
}

// A cheap fingerprint of everything editable (layer parameters, a sample of their pixels, the pieces).
uint64_t StateKey(const App& a) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    auto mixf = [&](float f) { uint32_t u; std::memcpy(&u, &f, 4); mix(u); };
    for (const auto& c : a.canvases) {
        mix(c.layers.size());
        for (const Layer& L : c.layers) {
            mix(L.visible); mix(L.isText); mix(L.isBase); mix(L.isPaint);
            mixf(L.x); mixf(L.y); mixf(L.scale); mixf(L.sx); mixf(L.sy); mixf(L.fontPx);
            for (int k = 0; k < 4; ++k) mix(L.color[k]);
            for (char ch : L.text) mix(static_cast<uint8_t>(ch));
            for (char ch : L.name) mix(static_cast<uint8_t>(ch));
            mix(L.src.width); mix(L.src.height);
            // A drawing changes a few pixels at a time: hash all of them (64 KB at most), sample the rest.
            for (size_t i = 0; i < L.src.rgba.size(); i += L.isPaint ? 1 : 97) mix(L.src.rgba[i]);
        }
    }
    for (int v : a.pieces.color) mix(static_cast<uint32_t>(v));
    for (int v : a.pieces.rgb) mix(static_cast<uint32_t>(v));
    for (int v : {a.pieces.lineType, a.pieces.number, a.pieces.outline, a.pieces.previewNumber, a.pieces.chest,
                  a.pieces.chestDx, a.pieces.chestDy, a.pieces.backDx, a.pieces.backDy, a.pieces.backSize})
        mix(static_cast<uint32_t>(v));
    mix(a.piecesOn);
    for (int v : a.backup) mix(static_cast<uint32_t>(v));
    return h;
}

void TrackHistory(App& a) {
    const uint64_t key = StateKey(a);
    if (!a.stableInit) {
        a.stable = Snap(a);
        a.stableKey = key;
        a.savedKey = key;
        a.stableInit = true;
        return;
    }
    const bool idle = !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                      !ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (key == a.stableKey || !idle) return;
    a.undo.push_back(std::move(a.stable));
    if (a.undo.size() > 40) a.undo.erase(a.undo.begin());
    a.redo.clear();
    a.stable = Snap(a);
    a.stableKey = key;
}

void Undo(App& a) {
    if (a.undo.empty()) return;
    a.redo.push_back(Snap(a));
    Restore(a, a.undo.back());
    a.undo.pop_back();
    a.stable = Snap(a);
    a.stableKey = StateKey(a);
}

void Redo(App& a) {
    if (a.redo.empty()) return;
    a.undo.push_back(Snap(a));
    Restore(a, a.redo.back());
    a.redo.pop_back();
    a.stable = Snap(a);
    a.stableKey = StateKey(a);
}

bool Dirty(const App& a) { return a.stableInit && StateKey(a) != a.savedKey; }

// ---- Layers -------------------------------------------------------------------------------------------
Layer* Selected(App& a) {
    Canvas& c = Active(a);
    return c.selected >= 0 && c.selected < static_cast<int>(c.layers.size()) ? &c.layers[c.selected] : nullptr;
}

void DeleteSelected(App& a) {
    Canvas& c = Active(a);
    if (!Selected(a)) return;
    c.layers.erase(c.layers.begin() + c.selected);
    c.selected = -1;
}

void DuplicateSelected(App& a) {
    Canvas& c = Active(a);
    Layer* L = Selected(a);
    if (!L || L->isBase || L->isPaint) return;
    Layer copy = *L;
    copy.x += 4;
    copy.y += 4;
    c.layers.insert(c.layers.begin() + c.selected + 1, std::move(copy));
    ++c.selected;
}

void MoveSelected(App& a, int step) {
    Canvas& c = Active(a);
    const int to = c.selected + step;
    if (!Selected(a) || to < 0 || to >= static_cast<int>(c.layers.size())) return;
    if (c.layers[static_cast<size_t>(to)].isBase) return;  // the base stays at the bottom
    std::swap(c.layers[static_cast<size_t>(c.selected)], c.layers[static_cast<size_t>(to)]);
    c.selected = to;
}

// ---- New / open / save -------------------------------------------------------------------------------
fs::path UniformsDir(const App& a) { return a.userData / "arrange" / "uniforms"; }

void NewUniform(App& a) {
    for (int i = 0; i < kKindCount; ++i) a.canvases[i] = Canvas{static_cast<Kind>(i), {}, -1};
    a.pieces = Pieces{};
    a.piecesOn = false;
    a.backup = {};
    std::snprintf(a.name, sizeof a.name, "%s", "nuevo");
    a.leftTab = 0;
    a.zone = 0;
    a.activeKind = 0;
    a.nav = {};
    a.undo.clear();
    a.redo.clear();
    a.stableInit = false;
    Toast(a, Tr("Uniforme nuevo.", "New uniform."));
}

// An older uniform (no project): each saved atlas comes back as the base image of its part, with its
// number colours / layout; the separate logos and the piece colours are not recoverable.
void OpenLegacy(App& a, const SavedUniform& u) {
    const fs::path dir = a.tempDir / "readppu";
    const BridgeResult r = ReadPpu(a.pyExe, a.toolDir, a.tempDir, u.ppu, dir);
    if (!r.ok) {
        Toast(a, r.message, true);
        return;
    }
    for (int i = 0; i < kKindCount; ++i) a.canvases[i] = Canvas{static_cast<Kind>(i), {}, -1};
    a.pieces = Pieces{};
    a.piecesOn = false;
    a.backup = {};
    for (int k = 0; k < kKindCount; ++k) {
        std::string err;
        auto img = tstudio::LoadImage(dir / (std::string(Info(static_cast<Kind>(k)).id) + ".png"), &err);
        if (!img) continue;
        Layer L;
        L.name = Tr("Uniforme guardado", "Saved uniform");
        L.src = std::move(*img);
        const int keep = a.activeKind;
        a.activeKind = k;
        SetBase(a, std::move(L));
        a.activeKind = keep;
    }
    std::ifstream meta(dir / "meta.txt");
    std::string line;
    int flags = 0;
    while (std::getline(meta, line)) {
        std::istringstream in(line);
        std::string key;
        in >> key;
        if (key == "backup") for (int& b : a.backup) in >> b;
        else if (key == "flags") in >> flags;
        else if (key == "layout")
            in >> a.pieces.chest >> a.pieces.chestDx >> a.pieces.chestDy >> a.pieces.backDx >> a.pieces.backDy >> a.pieces.backSize;
    }
    if (flags & 1) {
        a.pieces.number = a.backup[8];
        a.pieces.outline = a.backup[9];
    }
    std::snprintf(a.name, sizeof a.name, "%s", u.name.c_str());
    for (auto& c : a.canvases) c.selected = -1;  // nothing picked: the base is not dragged by accident
    a.zone = 0;
    a.activeKind = 0;
    a.nav = {};
    a.undo.clear();
    a.redo.clear();
    a.stableInit = false;
    Toast(a, std::string(Tr("Abierto (uniforme antiguo: cada parte es una sola imagen): ",
                            "Opened (older uniform: each part is one image): ")) + u.name);
}

void OpenUniform(App& a, const SavedUniform& u) {
    if (u.legacy) {
        OpenLegacy(a, u);
        return;
    }
    Project p;
    std::string err;
    if (!LoadProject(u.studio, &p, &err)) {
        Toast(a, std::string(Tr("No se pudo abrir: ", "Could not open: ")) + err, true);
        return;
    }
    a.canvases = std::move(p.canvases);
    a.pieces = p.pieces;
    a.piecesOn = p.piecesOn && EnsureGameBase(a);
    a.backup = p.backup;
    std::snprintf(a.name, sizeof a.name, "%s", u.name.c_str());
    if (a.piecesOn) ApplyPieces(a);  // the base layer from the pieces, as saved
    for (auto& c : a.canvases) c.selected = -1;
    a.zone = 0;
    a.activeKind = 0;
    a.nav = {};
    a.undo.clear();
    a.redo.clear();
    a.stableInit = false;
    Toast(a, std::string(Tr("Abierto: ", "Opened: ")) + u.name);
}

void SaveAll(App& a) {
    Save(a);
    if (a.toastError) return;
    // The editable project next to the .ppu, so the uniform can be opened again with its layers.
    const fs::path studio = UniformsDir(a) / SanitizeName(a.name) / "studio";
    std::string err;
    Project p{a.name, a.canvases, a.pieces, a.piecesOn, a.backup};
    if (!SaveProject(studio, p, &err)) {
        Toast(a, std::string(Tr("Guardado el uniforme, pero no el proyecto: ", "Uniform saved, project not: ")) + err, true);
        return;
    }
    std::error_code ec;
    fs::copy_file(a.tempDir / "thumb.png", studio / "thumb.png", fs::copy_options::overwrite_existing, ec);
    a.savedKey = StateKey(a);
}

// ---- Header ---------------------------------------------------------------------------------------------
void DrawBand(App& a, float w) {
    using namespace power_pros_ui;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = 64;
    const int start = dl->VtxBuffer.Size;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32_WHITE);
    Shade(dl, start, 0, h, ui::kBandTop, ui::kBandBottom);
    dl->AddLine(ImVec2(0, h), ImVec2(w, h), Rgba(255, 255, 255, 140), 3);
    ShadowedText(dl, g_title, 26, ImVec2(18, 8), IM_COL32_WHITE, "POWER PROS WII 2008");
    Text(dl, g_bold, 13, ImVec2(20, 40), ui::kYellow, "U N I F O R M   S T U D I O");

    // File actions, the name and undo / redo, always at hand.
    ImGui::SetCursorScreenPos(ImVec2(330, 16));
    if (ui::Pill(Tr("Nuevo", "New"), ui::kGreyTop, ui::kGreyBottom, true)) a.confirmNew = true;
    ImGui::SameLine();
    if (ui::Pill(Tr("Abrir", "Open"), ui::kBlueTop, ui::kBlueBottom, true)) {
        a.openList = ListProjects(UniformsDir(a));
        a.openThumbsDirty = true;
        a.showOpen = true;
    }
    ImGui::SameLine(0, 14);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##name", Tr("Nombre del uniforme", "Uniform name"), a.name, sizeof a.name);
    ImGui::SameLine();
    if (ui::Pill(Dirty(a) ? Tr("Guardar *", "Save *") : Tr("Guardar", "Save"), ui::kOrangeTop, ui::kOrangeBottom, true))
        SaveAll(a);
    ImGui::SameLine(0, 14);
    const bool canUndo = !a.undo.empty(), canRedo = !a.redo.empty();
    if (ui::Pill(Tr("Deshacer", "Undo"), canUndo ? ui::kBlueTop : ui::kGreyTop, canUndo ? ui::kBlueBottom : ui::kGreyBottom,
                 true, canUndo) && canUndo)
        Undo(a);
    ImGui::SameLine();
    if (ui::Pill(Tr("Rehacer", "Redo"), canRedo ? ui::kBlueTop : ui::kGreyTop, canRedo ? ui::kBlueBottom : ui::kGreyBottom,
                 true, canRedo) && canRedo)
        Redo(a);

    ImGui::SetCursorScreenPos(ImVec2(w - 100, 14));
    Lang lang = g_lang;
    if (ui::LangSwitch(&lang)) {
        g_lang = lang;
        if (fs::exists(a.config))
            tstudio::WriteSetting(a.config, "ui", "language", lang == Lang::En ? "\"en\"" : "\"es\"");
    }
}

// ---- Left: colours, logos and text, number -------------------------------------------------------------
bool Tab(const char* label, bool on) {
    return ui::Pill(label, on ? ui::kBlueTop : ui::kGreyTop, on ? ui::kBlueBottom : ui::kGreyBottom, true);
}

void DrawStart(App& a) {
    // No base yet: the three ways to start a uniform.
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", Tr("Empieza el uniforme con los colores del juego, una plantilla o un equipo de tu juego.",
                                 "Start the uniform with the game's colours, a template or a team from your game."));
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 4));
    if (ui::Pill(Tr("Colores por pieza (como el juego)", "Colour by piece (as the game)"), ui::kGreenTop, ui::kGreenBottom) &&
        EnsureGameBase(a))
        ApplyPieces(a);
    ImGui::Dummy(ImVec2(0, 4));
    static const power_pros_ui::Phrase tpl[] = {{"Plantilla...", "Template..."}, {"Blanco liso", "Plain white"},
                                                {"Pinstripes", "Pinstripes"}, {"Dos tonos", "Two-tone"},
                                                {"Franja/placket", "Placket stripe"}};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##tpl", tpl[a.templateChoice].get())) {
        for (int i = 1; i < 5; ++i)
            if (ImGui::Selectable(tpl[i].get(), i == a.templateChoice)) {
                a.templateChoice = i;
                const int keep = a.activeKind;
                a.activeKind = 0;
                ApplyTemplate(a, static_cast<Template>(i));
                a.activeKind = keep;
            }
        ImGui::EndCombo();
    }
    ImGui::Dummy(ImVec2(0, 4));
    if (ui::Pill(Tr("Copiar un equipo de mi juego...", "Copy a team from my game..."), ui::kGreyTop, ui::kGreyBottom, true))
        a.pendingGamePack = true;
}

void DrawColoursTab(App& a) {
    const bool hasBase = !a.canvases[0].layers.empty() && a.canvases[0].layers[0].isBase;
    if (!a.piecesOn) {
        DrawStart(a);
        if (hasBase) {
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("%s", Tr("La base actual es una plantilla, un equipo copiado o un uniforme guardado.",
                                         "The current base is a template, a copied team or a saved uniform."));
            ImGui::PopTextWrapPos();
        }
        return;
    }
    DrawPieces(a, 0);
    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::CollapsingHeader(Tr("Otra base", "Another base"))) DrawStart(a);
}

void DrawLayersTab(App& a) {
    const Zone* z = a.zone >= 0 && a.zone < static_cast<int>(a.zones.size()) ? &a.zones[a.zone] : nullptr;
    if (z) ImGui::TextDisabled(Tr("Se coloca en: %s", "Goes into: %s"), Tr(z->es, z->en));
    if (ui::Pill(Tr("Importar imagen", "Import image"), ui::kBlueTop, ui::kBlueBottom, true)) a.pendingImport = true;
    ImGui::SameLine();
    if (ui::Pill(Tr("Añadir texto", "Add text"), ui::kGreenTop, ui::kGreenBottom, true)) AddText(a);
    ImGui::Dummy(ImVec2(0, 6));
    Canvas& c = Active(a);
    if (c.layers.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", Tr("Esta parte no tiene capas. Elige una zona arriba del molde e importa un logo o añade texto.",
                                     "This part has no layers. Pick a zone above the mould and import a logo or add text."));
        ImGui::PopTextWrapPos();
        return;
    }
    // Top-most first, as they are stacked.
    for (int i = static_cast<int>(c.layers.size()) - 1; i >= 0; --i) {
        Layer& L = c.layers[static_cast<size_t>(i)];
        ImGui::PushID(i);
        bool vis = L.visible;
        if (ImGui::Checkbox("##vis", &vis)) L.visible = vis;
        ImGui::SameLine();
        std::string label = L.isBase ? std::string(Tr("Base: ", "Base: ")) + L.name
                            : L.isText ? std::string(Tr("Texto: ", "Text: ")) + L.text
                                       : L.name;
        if (label.size() > 30) label = label.substr(0, 29) + "...";
        if (ImGui::Selectable(label.c_str(), c.selected == i)) c.selected = i;
        ImGui::PopID();
    }
}

void DrawNumberTab(App& a) {
    if (!a.piecesOn) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", Tr("El número (colores y posición) se edita con «Colores por pieza».",
                                     "The number (colours and place) is edited with \"Colour by piece\"."));
        ImGui::PopTextWrapPos();
        if (ui::Pill(Tr("Activar colores por pieza", "Turn on colour by piece"), ui::kGreenTop, ui::kGreenBottom, true) &&
            EnsureGameBase(a))
            ApplyPieces(a);
        return;
    }
    DrawPieces(a, 1);
}

// The zone being edited as a paint mask, when "only inside the zone" is on and it belongs to this part.
const std::vector<uint8_t>* ZoneMask(App& a) {
    if (!a.clipZone || a.zone < 0 || a.zone >= static_cast<int>(a.zones.size())) return nullptr;
    const Zone& z = a.zones[a.zone];
    return static_cast<int>(z.kind) == a.activeKind && !z.mask.empty() ? &z.mask : nullptr;
}

// The drawing tools, by the mould (canvas.h Tool order) with their keyboard letter.
const power_pros_ui::Phrase kTools[] = {{"Pincel", "Brush"}, {"Línea", "Line"}, {"Rectángulo", "Rectangle"},
                                        {"Rect. lleno", "Filled rect."}, {"Rellenar", "Fill"},
                                        {"Borrador", "Eraser"}, {"Cuentagotas", "Picker"}};
const char* const kToolKeys[] = {"B", "L", "R", "Shift+R", "F", "E", "I"};

// Equal-width buttons in rows of `cols`; the one at `*value` lit. Returns true when it changed.
bool ButtonGrid(const char* id, const power_pros_ui::Phrase* labels, int n, int cols, int* value, float height,
                const char* const* tips = nullptr) {
    bool changed = false;
    const float gap = 4, w = (ImGui::GetContentRegionAvail().x - gap * (cols - 1)) / cols;
    ImGui::PushID(id);
    for (int i = 0; i < n; ++i) {
        if (i % cols) ImGui::SameLine(0, gap);
        const bool on = *value == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::ColorConvertU32ToFloat4(ui::kBlueBottom)
                                                  : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Border, on ? ImGui::ColorConvertU32ToFloat4(ui::kYellow)
                                                  : ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, on ? 2.0f : 1.0f);
        ImGui::PushID(i);
        if (ImGui::Button(labels[i].get(), ImVec2(w, height)) && !on) {
            *value = i;
            changed = true;
        }
        if (tips && ImGui::IsItemHovered()) ImGui::SetTooltip("%s  (%s)", labels[i].get(), tips[i]);
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
    return changed;
}

// Above the mould while drawing: the tools in one row (two when narrow), then the colour in use, the
// width, "only inside the zone" and the view.
void DrawPaintToolbar(App& a) {
    float widest = 0;
    for (const auto& t : kTools) widest = std::max(widest, ImGui::CalcTextSize(t.get()).x);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int perRow = std::clamp(static_cast<int>((avail + 4) / (widest + 24 + 4)), 1, 7);
    const int cols = (7 + (7 + perRow - 1) / perRow - 1) / ((7 + perRow - 1) / perRow);  // same count per row, as near as can be
    int tool = static_cast<int>(a.paint.tool);
    if (ButtonGrid("tools", kTools, 7, cols, &tool, 34, kToolKeys)) a.paint.tool = static_cast<Tool>(tool);
    ImGui::Dummy(ImVec2(0, 2));

    const ImVec4 cur(a.paint.color[0] / 255.0f, a.paint.color[1] / 255.0f, a.paint.color[2] / 255.0f, 1.0f);
    ImGui::ColorButton("##cur", cur, ImGuiColorEditFlags_NoTooltip, ImVec2(44, ImGui::GetFrameHeight()));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("Color del dibujo (se cambia a la izquierda o con el Cuentagotas)",
                                   "Drawing colour (change it on the left or with the Picker)"));
    ImGui::SameLine(0, 10);
    ImGui::SetNextItemWidth(std::clamp(avail * 0.3f, 120.0f, 220.0f));
    ImGui::SliderFloat("##size", &a.paint.size, 1.0f, 64.0f, Tr("grosor %.0f px", "width %.0f px"), ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr("También con la rueda sobre el molde, o [ y ].", "Also the wheel over the mould, or [ and ]."));
    ImGui::SameLine(0, 10);
    ImGui::Checkbox(Tr("Solo en la zona", "Only in the zone"), &a.clipZone);
    if (ImGui::IsItemHovered()) {
        const Zone* z = a.zone >= 0 && a.zone < static_cast<int>(a.zones.size()) ? &a.zones[a.zone] : nullptr;
        ImGui::SetTooltip(Tr("Lo pintado no se sale de: %s", "Nothing spills out of: %s"), z ? Tr(z->es, z->en) : "-");
    }
    // The view, at the right end (next line if it does not fit).
    const ImGuiStyle& s = ImGui::GetStyle();
    const char* fit = Tr("Ver todo", "Fit");
    const float viewW = ImGui::CalcTextSize(" - ").x + ImGui::CalcTextSize(" + ").x + ImGui::CalcTextSize(fit).x +
                        6 * s.FramePadding.x + 8;
    ImGui::SameLine(0, 10);
    const float x0 = ImGui::GetCursorPosX(), end = x0 + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetContentRegionAvail().x < viewW) ImGui::NewLine();
    else ImGui::SetCursorPosX(end - viewW);
    if (ImGui::Button(" - ")) a.nav.zoom = std::max(1.0f, a.nav.zoom / 1.5f);
    ImGui::SameLine(0, 4);
    if (ImGui::Button(" + ")) a.nav.zoom = std::min(16.0f, a.nav.zoom * 1.5f);
    ImGui::SameLine(0, 4);
    if (ImGui::Button(fit)) a.nav = {};
}

// Line patterns (stripes, diagonals, checks) in the drawing colour, previewed on the mould before they
// land in the drawing layer. In the right panel while drawing.
void DrawPattern(App& a) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    static const power_pros_ui::Phrase pats[] = {{"Verticales", "Vertical"}, {"Horizontales", "Horizontal"},
                                                 {"Diagonal /", "Diagonal /"}, {"Diagonal \\", "Diagonal \\"},
                                                 {"Cuadros", "Checks"}};
    ButtonGrid("pats", pats, 5, 3, &a.pattern, 30);
    ImGui::SetNextItemWidth(-1);
    if (a.pattern != 4) ImGui::SliderFloat("##pw", &a.patWidth, 1.0f, 32.0f, Tr("grosor de la línea: %.0f px", "line width: %.0f px"));
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##ps", &a.patSpacing, 2.0f, 64.0f,
                       a.pattern == 4 ? Tr("tamaño del cuadro: %.0f px", "square size: %.0f px")
                                      : Tr("una línea cada %.0f px", "a line every %.0f px"));
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##po", &a.patOffset, 0.0f, a.patSpacing, Tr("desplazar: %.0f px", "shift: %.0f px"));
    const bool apply = ui::Pill(Tr("Aplicar patrón", "Apply pattern"), ui::kGreenTop, ui::kGreenBottom, true);
    ImGui::SameLine(0, 12);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    ImGui::Checkbox(Tr("Vista previa", "Preview"), &a.patPreview);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("Muestra el patrón en el molde y en el muñeco antes de aplicarlo.",
                                   "Shows the pattern on the mould and the figure before applying it."));
    if (apply) {
        const Rgba8 c{a.paint.color[0], a.paint.color[1], a.paint.color[2], 255};
        PaintPattern(PaintLayer(Active(a), Tr("Dibujo", "Drawing")).src, static_cast<Pattern>(a.pattern), a.patWidth,
                     a.patSpacing, a.patOffset, c, PaintMask{ZoneMask(a)});
        a.patPreview = false;
        Toast(a, Tr("Patrón aplicado (Ctrl+Z lo deshace).", "Pattern applied (Ctrl+Z undoes it)."));
    }

    ImGui::PopStyleVar();
}

// Drawing, left side: the colour and the drawing layer (tools above the mould, patterns on the right).
// Everything goes into the part's drawing layer (canvas.h PaintLayer), baked like any other layer.
void DrawPaintTab(App& a) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 7));
    const float avail = ImGui::GetContentRegionAvail().x;

    ImGui::SeparatorText(Tr("Color del dibujo", "Drawing colour"));
    float f[3] = {a.paint.color[0] / 255.0f, a.paint.color[1] / 255.0f, a.paint.color[2] / 255.0f};
    ImGui::SetNextItemWidth(std::min(avail, 210.0f));
    if (ImGui::ColorPicker3("##pc", f, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayRGB |
                                           ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueBar))
        for (int k = 0; k < 3; ++k) a.paint.color[k] = static_cast<unsigned char>(std::lround(f[k] * 255));
    // Quick colours: the uniform's pieces, then the game's 32 in the game's 8 x 4 grid.
    const float sw = std::min(24.0f, (avail - 7 * 4) / 8);
    auto swatch = [&](int id, uint32_t rgb) {
        ImGui::PushID(id);
        if (ImGui::ColorButton("##s", Rgb4(rgb), 0, ImVec2(sw, sw)))
            for (int k = 0; k < 3; ++k) a.paint.color[k] = static_cast<unsigned char>(rgb >> (16 - 8 * k));
        ImGui::PopID();
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    if (a.gameBase.ok) {
        if (a.piecesOn) {
            ImGui::TextDisabled("%s", Tr("Colores del uniforme", "Uniform colours"));
            for (int p = 0; p < kPieceCount; ++p) {
                if (p) ImGui::SameLine();
                swatch(100 + p, PieceRgb(a.gameBase, a.pieces, p, 0));
            }
        }
        ImGui::TextDisabled("%s", Tr("Colores del juego", "Game colours"));
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 8; ++col) {
                if (col) ImGui::SameLine();
                swatch(col * 4 + row, ColorRgb(a.gameBase, col * 4 + row, 0));
            }
    }
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::SeparatorText(Tr("Capa de dibujo", "Drawing layer"));
    if (ui::Pill(Tr("Nueva capa", "New layer"), ui::kBlueTop, ui::kBlueBottom, true)) {
        Canvas& c = Active(a);
        Layer L;
        L.name = Tr("Dibujo", "Drawing");
        L.isPaint = true;
        const KindInfo& in = Info(c.kind);
        L.src = Image{static_cast<uint32_t>(in.w), static_cast<uint32_t>(in.h),
                      std::vector<uint8_t>(static_cast<size_t>(in.w) * in.h * 4, 0)};
        c.layers.push_back(std::move(L));
        c.selected = static_cast<int>(c.layers.size()) - 1;
    }
    const char* where = Tr("Se pinta en la capa de dibujo elegida en «Logos y texto» (o en la de más arriba).",
                           "Painting goes into the drawing layer picked in \"Logos & text\" (or the top one).");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", where);
    ImGui::SameLine();
    if (ui::Pill(Tr("Borrar el dibujo", "Clear the drawing"), ui::kRedTop, ui::kRedBottom, true))
        for (auto& px : PaintLayer(Active(a), Tr("Dibujo", "Drawing")).src.rgba) px = 0;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", where);
    ImGui::PopStyleVar();
}

void DrawLeft(App& a, float h, float w) {
    if (!ui::BeginPanel("##left", Tr("Uniforme", "Uniform"), ImVec2(w, h))) { ui::EndPanel(); return; }
    static const power_pros_ui::Phrase tabs[] = {{"Colores", "Colours"}, {"Logos y texto", "Logos & text"},
                                                 {"Número", "Number"}, {"Dibujo", "Drawing"}};
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < 4; ++i) {  // wrapping when the panel is narrow
        if (i > 0) {
            ImGui::SameLine(0, 6);
            if (ImGui::GetCursorScreenPos().x + ImGui::CalcTextSize(tabs[i].get()).x + 26 > right) ImGui::NewLine();
        }
        if (Tab(tabs[i].get(), a.leftTab == i)) a.leftTab = i;
    }
    ImGui::Separator();
    ImGui::BeginChild("##tab", ImVec2(0, 0), ImGuiChildFlags_None);
    if (a.leftTab == 0) DrawColoursTab(a);
    else if (a.leftTab == 1) DrawLayersTab(a);
    else if (a.leftTab == 2) DrawNumberTab(a);
    else DrawPaintTab(a);
    ImGui::EndChild();
    ui::EndPanel();
}

// ---- Centre: zones, toolbar, mould ------------------------------------------------------------------
void DrawCentre(App& a, ImVec2 size) {
    if (!ui::BeginPanel("##centre", Tr("Molde del uniforme", "Uniform mould"), size)) { ui::EndPanel(); return; }
    // The zones as buttons (one click), wrapping.
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < static_cast<int>(a.zones.size()); ++i) {
        const char* label = Tr(a.zones[i].es, a.zones[i].en);
        const float wbtn = ImGui::CalcTextSize(label).x + 26;
        if (i > 0) {
            ImGui::SameLine(0, 6);
            if (ImGui::GetCursorScreenPos().x + wbtn > right) ImGui::NewLine();
        }
        ImGui::PushID(i);
        if (Tab(label, a.zone == i)) {
            a.zone = i;
            a.nav = {};
            a.activeKind = static_cast<int>(a.zones[i].kind);
        }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 2));
    // Toolbar: the drawing tools while drawing; else the view, then what applies to the selected layer.
    if (a.leftTab == 3) {
        DrawPaintToolbar(a);
    } else {
    if (ImGui::Button(" - ")) a.nav.zoom = std::max(1.0f, a.nav.zoom / 1.5f);
    ImGui::SameLine(0, 4);
    if (ImGui::Button(" + ")) a.nav.zoom = std::min(16.0f, a.nav.zoom * 1.5f);
    ImGui::SameLine(0, 4);
    if (ImGui::Button(Tr("Ver todo", "Fit"))) a.nav = {};
    Layer* L = Selected(a);
    const bool placeable = L && !L->isPaint && !L->isBase;
    ImGui::BeginDisabled(!L);
    ImGui::SameLine(0, 16);
    ImGui::BeginDisabled(!placeable);
    if (ImGui::Button(Tr("Centrar", "Centre")) && placeable) CenterInZone(a, *L, true, true);
    ImGui::SameLine(0, 4);
    if (ImGui::Button(Tr("Ajustar a la zona", "Fit to zone")) && placeable) PlaceInZone(a, *L);
    ImGui::EndDisabled();
    ImGui::SameLine(0, 4);
    if (ImGui::Button(Tr("Duplicar", "Duplicate"))) DuplicateSelected(a);
    ImGui::SameLine(0, 4);
    if (ImGui::Button(Tr("Subir", "Up"))) MoveSelected(a, +1);
    ImGui::SameLine(0, 4);
    if (ImGui::Button(Tr("Bajar", "Down"))) MoveSelected(a, -1);
    ImGui::SameLine(0, 4);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::kRedBottom));
    if (ImGui::Button(Tr("Borrar", "Delete"))) DeleteSelected(a);
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    }
    ImGui::Dummy(ImVec2(0, 2));
    // The mould.
    View view;
    const Zone* z = a.zone >= 0 && a.zone < static_cast<int>(a.zones.size()) ? &a.zones[a.zone] : nullptr;
    if (z && static_cast<int>(z->kind) == a.activeKind) {
        const KindInfo& in = Info(Active(a).kind);
        view = {std::max(0.0f, z->x0 - 2), std::max(0.0f, z->y0 - 2), std::min<float>(in.w, z->x1 + 2),
                std::min<float>(in.h, z->y1 + 2)};
    }
    a.paint.mask = ZoneMask(a);
    a.paint.composed = &a.activeImg;
    DrawCanvas(Active(a), a.canvasTex[a.activeKind].t, a.hasGuide && a.activeKind == 0 ? a.guideTex.t : nullptr,
               z && !z->mask.empty() ? a.shadeTex.t : nullptr, ImGui::GetContentRegionAvail(), view, a.nav,
               a.leftTab == 3 ? &a.paint : nullptr);
    ui::EndPanel();
}

// ---- Right: the selected layer --------------------------------------------------------------------------
void DrawProperties(App& a, ImVec2 pos, ImVec2 size) {
    ImGui::SetCursorPos(pos);
    const bool drawing = a.leftTab == 3;
    if (!ui::BeginPanel("##props", drawing ? Tr("Patrón de líneas", "Line pattern") : Tr("Capa seleccionada", "Selected layer"), size)) {
        ui::EndPanel();
        return;
    }
    if (drawing) {
        DrawPattern(a);
        ui::EndPanel();
        return;
    }
    Layer* sel = Selected(a);
    if (!sel) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", Tr("Haz clic en un logo o texto del molde para editarlo.\n\nRaton: arrastrar mueve, esquinas "
                                     "cambian el tamaño, lados estiran, rueda agranda.\nTeclado: flechas mueven, Supr borra, "
                                     "Ctrl+D duplica, Ctrl+Z deshace, Ctrl+S guarda.",
                                     "Click a logo or text on the mould to edit it.\n\nMouse: drag moves, corners resize, "
                                     "sides stretch, wheel enlarges.\nKeys: arrows move, Del deletes, Ctrl+D duplicates, "
                                     "Ctrl+Z undoes, Ctrl+S saves."));
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Checkbox(Tr("Colores de respaldo (avanzado)", "Backup colours (advanced)"), &a.advanced);
        if (a.advanced)
            for (int i = 0; i < 10; ++i) {
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(150);
                ImGui::SliderInt("##bk", &a.backup[i], 0, 31);
                ImGui::PopID();
                if (i % 2 == 0) ImGui::SameLine();
            }
        ui::EndPanel();
        return;
    }
    Layer& L = *sel;
    if (L.isPaint) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", Tr("Capa de dibujo. Píntala en la pestaña «Dibujo»; aquí puedes ocultarla o borrarla.",
                                     "Drawing layer. Paint it in the \"Drawing\" tab; here you can hide or delete it."));
        ImGui::PopTextWrapPos();
        ImGui::Checkbox(Tr("Visible", "Visible"), &L.visible);
        if (ui::Pill(Tr("Dibujar en esta capa", "Draw on this layer"), ui::kBlueTop, ui::kBlueBottom, true)) a.leftTab = 3;
        ui::EndPanel();
        return;
    }
    if (L.isText) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s", L.text.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##t", buf, sizeof buf)) L.text = buf;
        ImGui::TextUnformatted(Tr("Tamaño de la letra", "Font size"));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##f", &L.fontPx, 4.0f, 120.0f, "%.0f");
        float col[3] = {L.color[0] / 255.0f, L.color[1] / 255.0f, L.color[2] / 255.0f};
        if (ImGui::ColorEdit3(Tr("Color", "Colour"), col, ImGuiColorEditFlags_NoInputs)) {
            for (int k = 0; k < 3; ++k) L.color[k] = static_cast<unsigned char>(col[k] * 255);
        }
    } else {
        ImGui::TextUnformatted(Tr("Tamaño", "Size"));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##s", &L.scale, 0.005f, 4.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::TextUnformatted(Tr("Ancho / alto", "Width / height"));
        const float half = (ImGui::GetContentRegionAvail().x - 8) / 2;
        ImGui::SetNextItemWidth(half);
        ImGui::SliderFloat("##sx", &L.sx, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine(0, 8);
        ImGui::SetNextItemWidth(half);
        ImGui::SliderFloat("##sy", &L.sy, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::SmallButton(Tr("Forma original", "Original shape"))) L.sx = L.sy = 1.0f;
    }
    ImGui::TextUnformatted(Tr("Posición", "Position"));
    const float half = (ImGui::GetContentRegionAvail().x - 8) / 2;
    ImGui::SetNextItemWidth(half);
    ImGui::DragFloat("##x", &L.x, 0.25f, -256.0f, 256.0f, "X %.1f");
    ImGui::SameLine(0, 8);
    ImGui::SetNextItemWidth(half);
    ImGui::DragFloat("##y", &L.y, 0.25f, -256.0f, 256.0f, "Y %.1f");
    if (ImGui::SmallButton(Tr("Centrar", "Centre"))) CenterInZone(a, L, true, true);
    ImGui::SameLine();
    if (ImGui::SmallButton(Tr("Centrar horizontal", "Centre across"))) CenterInZone(a, L, true, false);
    ImGui::SameLine();
    if (ImGui::SmallButton(Tr("Vertical", "Up/down"))) CenterInZone(a, L, false, true);
    ui::EndPanel();
}

// ---- Keyboard ----------------------------------------------------------------------------------------
void Shortcuts(App& a) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { io.KeyShift ? Redo(a) : Undo(a); }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo(a);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveAll(a);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateSelected(a);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) DeleteSelected(a);
    if (a.leftTab == 3 && !io.KeyCtrl) {  // drawing: a letter per tool (kToolKeys), [ ] the width
        const ImGuiKey keys[] = {ImGuiKey_B, ImGuiKey_L, ImGuiKey_R, ImGuiKey_R, ImGuiKey_F, ImGuiKey_E, ImGuiKey_I};
        for (int i = 0; i < 7; ++i)
            if (ImGui::IsKeyPressed(keys[i], false) && (i != 2 || !io.KeyShift) && (i != 3 || io.KeyShift))
                a.paint.tool = static_cast<Tool>(i);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) a.paint.size = std::max(1.0f, a.paint.size / 1.25f);
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) a.paint.size = std::min(64.0f, a.paint.size * 1.25f);
    }
}

// ---- Open / new dialogs -------------------------------------------------------------------------------
void DrawDialogs(App& a, ImVec2 vs) {
    if (a.showOpen) {
        ImGui::OpenPopup("##open");
        a.showOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(std::min(560.0f, vs.x - 40), std::min(520.0f, vs.y - 40)));
    ImGui::SetNextWindowPos(ImVec2(vs.x / 2, vs.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##open", nullptr, ImGuiWindowFlags_NoDecoration)) {
        ImGui::TextUnformatted(Tr("Abrir un uniforme guardado", "Open a saved uniform"));
        ImGui::Separator();
        ImGui::BeginChild("##list", ImVec2(0, -44));
        if (a.openList.empty())
            ImGui::TextDisabled("%s", Tr("Todavía no hay uniformes guardados con este editor.",
                                         "No uniforms saved with this editor yet."));
        // Cards: the figure wearing it and its name, as many per row as fit.
        const float card = 132, right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (size_t i = 0; i < a.openList.size(); ++i) {
            const SavedUniform& u = a.openList[i];
            if (i > 0) {
                ImGui::SameLine(0, 10);
                if (ImGui::GetCursorScreenPos().x + card > right) ImGui::NewLine();
            }
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const bool pick = ImGui::Selectable("##card", false, 0, ImVec2(card, card + 26));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(at.x + 4, at.y + 4), ImVec2(at.x + card - 4, at.y + card - 4), power_pros_ui::Rgba(240, 244, 250), 8);
            if (i < a.openThumbs.size() && a.openThumbs[i].t)
                dl->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(a.openThumbs[i].t)), ImVec2(at.x + 10, at.y + 10),
                             ImVec2(at.x + card - 10, at.y + card - 10));
            if (u.legacy) {  // saved before projects: a corner tag (and a word if it has no thumbnail either)
                const char* old = Tr("antiguo", "older");
                const bool bare = i >= a.openThumbs.size() || !a.openThumbs[i].t;
                const ImVec2 tp = bare ? ImVec2(at.x + (card - ImGui::CalcTextSize(old).x) / 2, at.y + card / 2 - 8)
                                       : ImVec2(at.x + 10, at.y + 8);
                dl->AddText(tp, power_pros_ui::Rgba(90, 100, 120), old);
            }
            std::string name = u.name.size() > 16 ? u.name.substr(0, 15) + "..." : u.name;
            dl->AddText(ImVec2(at.x + (card - ImGui::CalcTextSize(name.c_str()).x) / 2, at.y + card + 2),
                        ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            if (pick) {
                if (Dirty(a)) Toast(a, Tr("Los cambios sin guardar del uniforme anterior se perdieron.",
                                          "Unsaved changes of the previous uniform were dropped."));
                OpenUniform(a, u);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (ui::Pill(Tr("Cancelar", "Cancel"), ui::kGreyTop, ui::kGreyBottom, true) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (a.confirmNew) {
        a.confirmNew = false;
        if (Dirty(a)) ImGui::OpenPopup("##new");
        else NewUniform(a);
    }
    ImGui::SetNextWindowPos(ImVec2(vs.x / 2, vs.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##new", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(Tr("Hay cambios sin guardar. ¿Empezar uno nuevo igual?",
                                  "There are unsaved changes. Start a new one anyway?"));
        if (ui::Pill(Tr("Guardar y nuevo", "Save and new"), ui::kOrangeTop, ui::kOrangeBottom, true)) {
            SaveAll(a);
            if (!a.toastError) NewUniform(a);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::Pill(Tr("Descartar", "Discard"), ui::kRedTop, ui::kRedBottom, true)) {
            NewUniform(a);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::Pill(Tr("Cancelar", "Cancel"), ui::kGreyTop, ui::kGreyBottom, true)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

int Run(const std::vector<std::string>& args) {
#if defined(_WIN32)
    SetProcessDPIAware();
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window* window = SDL_CreateWindow("Power Pros Wii 2008 Uniform Studio", 1440, 880,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_SetWindowMinimumSize(window, 1180, 720);
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
    for (size_t i = 0; i + 1 < args.size(); ++i)  // the launcher passes its own Config.toml
        if (args[i] == "--config") a.config = tstudio::PathFromUtf8(args[i + 1]);
    a.tempDir = a.userData / "Cache" / "uniform_studio";
    for (int i = 0; i < kKindCount; ++i) a.canvases[i].kind = static_cast<Kind>(i);
    a.mesh = BuildMannequin();
    EnsureGameDoll(a);
    LoadGameBase(GameBaseCache(a), &a.gameBase);  // if already read; else on "colour by piece"
    a.zones = BuildZones(&a.gameBase, &a.doll);

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
    for (size_t i = 0; i + 1 < args.size(); ++i)  // tests / screenshots: open a left tab
        if (args[i] == "--tab") a.leftTab = std::clamp(std::atoi(args[i + 1].c_str()), 0, 3);
    for (const auto& arg : args)  // the piece palette port, on synthetic colours (no game data)
        if (arg == "--pieces-selftest") {
            GameBase g;
            for (int i = 0; i < 32; ++i)
                for (int k = 0; k < 9; ++k) g.colors[i][k] = static_cast<uint8_t>((i * 37 + k * 11) & 0xF8);
            g.ok = true;
            Pieces p;
            p.color = {3, 4, 5, 6, 7, 8};
            p.lineType = 2;
            const auto pal = ComposeBodyPalette(g, p);
            bool ok = true;
            // Brim 0x52 is a 1/3 mix: 255 * 2/3 + 255 * 1/3 truncates to alpha 254 -> RGB4A3, as in the game.
            ok &= (pal[0x52] & 0x8000) == 0 && ((pal[0x52] >> 12) & 7) == 7;
            ok &= (pal[0x51] & 0x8000) != 0;  // plain colours are opaque RGB555
            // Pinstripes: 0x82 is the line colour, 0x80 the upper's.
            const auto line = ColorRgb(g, 8, 0), upper = ColorRgb(g, 6, 0);
            ok &= pal[0x82] == (0x8000 | ((line >> 19) & 31) << 10 | ((line >> 11) & 31) << 5 | ((line >> 3) & 31));
            ok &= pal[0x80] == (0x8000 | ((upper >> 19) & 31) << 10 | ((upper >> 11) & 31) << 5 | ((upper >> 3) & 31));
            std::printf("%s\n", ok ? "PIECES OK" : "PIECES FAIL");
            return ok ? 0 : 1;
        }
    for (const auto& arg : args)  // drawing (paint.h, the drawing layer) and free piece colours, synthetic data
        if (arg == "--paint-selftest") {
            bool ok = true;
            Image img{16, 8, std::vector<uint8_t>(16 * 8 * 4, 0)};
            const Rgba8 red{255, 0, 0, 255};
            PaintPattern(img, Pattern::Vertical, 2, 4, 0, red, {});
            auto alpha = [&](const Image& im, int x, int y) { return im.rgba[(static_cast<size_t>(y) * im.width + x) * 4 + 3]; };
            ok &= alpha(img, 0, 3) == 255 && alpha(img, 1, 3) == 255 && alpha(img, 2, 3) == 0 && alpha(img, 4, 3) == 255;
            std::vector<uint8_t> mask(16 * 8, 0);
            for (int x = 8; x < 16; ++x) mask[3 * 16 + x] = 1;  // only row 3, right half
            Image fill{16, 8, std::vector<uint8_t>(16 * 8 * 4, 0)};
            PaintFill(fill, nullptr, 10, 3, red, PaintMask{&mask});
            ok &= alpha(fill, 10, 3) == 255 && alpha(fill, 15, 3) == 255 && alpha(fill, 7, 3) == 0 && alpha(fill, 10, 2) == 0;
            // A drawing layer composes 1:1 over the atlas.
            Canvas c{Kind::Body, {}, -1};
            PaintDab(PaintLayer(c, "d").src, 40.5f, 30.5f, 1, red, false, {});
            const Image out = Compose(c);
            const uint8_t* px = &out.rgba[(30 * out.width + 40) * 4];
            ok &= px[0] == 255 && px[1] == 0 && px[3] == 255 && out.rgba[(30 * out.width + 41) * 4 + 3] == 0;
            // A free colour reaches the palette (RGB555) and the backup takes the nearest game colour.
            GameBase g;
            for (int i = 0; i < 32; ++i)
                for (int k = 0; k < 9; ++k) g.colors[i][k] = static_cast<uint8_t>((i * 37 + k * 11) & 0xF8);
            g.ok = true;
            Pieces p;
            p.rgb[kUpper] = 0x123456;
            ok &= ComposeBodyPalette(g, p)[0x80] == (0x8000 | (0x12 >> 3) << 10 | (0x34 >> 3) << 5 | (0x56 >> 3));
            ok &= NearestColor(g, ColorRgb(g, 9, 0)) == 9;
            std::printf("%s\n", ok ? "PAINT OK" : "PAINT FAIL");
            return ok ? 0 : 1;
        }
    for (const auto& arg : args)  // start from the game's pieces (screenshots / tests)
        if (arg == "--pieces" && EnsureGameBase(a)) ApplyPieces(a);
    for (size_t i = 0; i + 1 < args.size(); ++i)  // tests / screenshots: pick a zone, import an image into it
        if (args[i] == "--zone") {
            a.zone = std::clamp(std::atoi(args[i + 1].c_str()), 0, std::max(0, static_cast<int>(a.zones.size()) - 1));
            if (!a.zones.empty()) a.activeKind = static_cast<int>(a.zones[a.zone].kind);
        }
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--import") FinishImport(a, args[i + 1]);
    for (size_t i = 0; i + 1 < args.size(); ++i)  // screenshots / tests: piece,RRGGBB as a free colour; open its picker
        if (args[i] == "--free-rgb" && a.piecesOn) {
            int piece = 0;
            unsigned rgb = 0;
            if (std::sscanf(args[i + 1].c_str(), "%d,%x", &piece, &rgb) == 2 && piece >= 0 && piece < kPieceCount) {
                a.pieces.rgb[static_cast<size_t>(piece)] = static_cast<int>(rgb & 0xFFFFFF);
                a.pickingPiece = piece;
                ApplyPieces(a);
            }
        }
    for (const auto& arg : args)  // screenshots: pinstripes and a stroke drawn in the zone
        if (arg == "--paint-demo") {
            a.paint.color[0] = 20; a.paint.color[1] = 40; a.paint.color[2] = 160;
            Layer& L = PaintLayer(Active(a), Tr("Dibujo", "Drawing"));
            PaintPattern(L.src, Pattern::Vertical, 1, 5, 0, {20, 40, 160, 255}, PaintMask{ZoneMask(a)});
            PaintLine(L.src, 20, 20, 60, 50, 3, {220, 30, 40, 255}, false, PaintMask{ZoneMask(a)});
        }
    for (const auto& arg : args)  // screenshots: a diagonal pattern previewed, not applied
        if (arg == "--pattern-preview") {
            a.pattern = 2;
            a.patPreview = true;
        }
    for (size_t i = 0; i + 1 < args.size(); ++i)  // tests: chest,chestDx,chestDy,backDx,backDy,backSize
        if (args[i] == "--number-layout")
            std::sscanf(args[i + 1].c_str(), "%d,%d,%d,%d,%d,%d", &a.pieces.chest, &a.pieces.chestDx, &a.pieces.chestDy,
                        &a.pieces.backDx, &a.pieces.backDy, &a.pieces.backSize);
    for (const auto& arg : args)
        if (arg == "--inspect") { a.inspect = true; a.iyaw = a.yaw; }
    for (size_t i = 0; i + 1 < args.size(); ++i)  // tests: the body atlas with the sample number drawn
        if (args[i] == "--number-test" && a.piecesOn) {
            a.pieces.number = 4;
            a.pieces.outline = 21;
            a.pieces.previewNumber = 27;
            Image body = Compose(a.canvases[0]);
            DrawNumber(body, a.gameBase, a.pieces);
            std::string err;
            return tstudio::SavePng(tstudio::PathFromUtf8(args[i + 1]), body, &err) ? 0 : 1;
        }

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
        if (arg == "--save-test") {  // the editor's Save: the .ppu and its project
            if (Active(a).layers.empty()) LoadDemo(a);
            if (SanitizeName(a.name).empty()) std::snprintf(a.name, sizeof a.name, "%s", "nuevo");
            SaveAll(a);
            std::fprintf(a.toastError ? stderr : stdout, "%s: %s\n", a.toastError ? "ERROR" : "SAVED", a.toast.c_str());
            return a.toastError ? 1 : 0;
        }
    for (size_t i = 0; i + 1 < args.size(); ++i)  // tests / screenshots: open a saved uniform
        if (args[i] == "--open")
            for (const SavedUniform& u : ListProjects(UniformsDir(a)))
                if (u.name == args[i + 1]) OpenUniform(a, u);
    for (const auto& arg : args)
        if (arg == "--open-dialog") {
            a.openList = ListProjects(UniformsDir(a));
            a.openThumbsDirty = true;
            a.showOpen = true;
        }

    int undoTest = 0;
    bool undoOk = false;
    for (const auto& arg : args)
        if (arg == "--undo-selftest" && a.piecesOn) undoTest = 1;
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

        if (a.openThumbsDirty) {  // the open dialog's thumbnails
            for (auto& t : a.openThumbs) if (t.t) SDL_DestroyTexture(t.t);
            a.openThumbs.assign(a.openList.size(), Tex{});
            for (size_t i = 0; i < a.openList.size(); ++i)
                if (!a.openList[i].thumb.Empty()) Upload(renderer, a.openThumbs[i], a.openList[i].thumb);
            a.openThumbsDirty = false;
        }
        if (a.shadeZone != a.zone && a.zone >= 0 && a.zone < static_cast<int>(a.zones.size()) &&
            !a.zones[a.zone].mask.empty()) {
            const Zone& z = a.zones[a.zone];
            Image shade{128, 128, std::vector<uint8_t>(128 * 128 * 4, 0)};
            for (size_t i = 0; i < z.mask.size(); ++i)
                if (!z.mask[i]) shade.rgba[i * 4 + 3] = 150;  // outside the zone: darkened
            Upload(renderer, a.shadeTex, shade);
            a.shadeZone = a.zone;
        }
        // Recompose the active atlas (for its canvas) and the body atlas (for the preview).
        if (a.leftTab == 3 && a.patPreview) {
            Canvas tmp = Active(a);
            const Rgba8 c{a.paint.color[0], a.paint.color[1], a.paint.color[2], 255};
            PaintPattern(PaintLayer(tmp, "preview").src, static_cast<Pattern>(a.pattern), a.patWidth, a.patSpacing,
                         a.patOffset, c, PaintMask{ZoneMask(a)});
            a.activeImg = Compose(tmp);
        } else {
            a.activeImg = Compose(Active(a));
        }
        Upload(renderer, a.canvasTex[a.activeKind], a.activeImg);
        const Image bodyImg = a.activeKind == 0 ? a.activeImg : Compose(a.canvases[0]);
        Upload(renderer, a.bodyPreviewTex, bodyImg);
        const int colors = CountColors(bodyImg);
        if (a.dollView) {
            if (a.doll.ok) {  // the game's own figure, wearing the composed body atlas and cap letter
                Image capImg = Compose(a.canvases[1]);
                bool capUsed = false;
                for (size_t i = 3; i < capImg.rgba.size(); i += 4) capUsed |= capImg.rgba[i] >= 128;
                if (capUsed) FillBehind(capImg, CapFill(a, bodyImg));  // as the game shows it
                std::vector<const Image*> extras{&capImg};
                for (const Image& t : a.doll.textures) extras.push_back(&t);
                Image withNumber = bodyImg;
                if (a.piecesOn) DrawNumber(withNumber, a.gameBase, a.pieces);
                Upload(renderer, a.render3dTex,
                       RenderMesh(a.doll.mesh, withNumber, 440, 440, a.yaw, a.pitch, a.zoom, 4.0f, &extras));
            } else {
                Upload(renderer, a.render3dTex, RenderMesh(a.mesh, bodyImg, 440, 440, a.yaw, a.pitch, a.zoom));
            }
        }

        const ImVec2 vs = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(vs);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        DrawBand(a, vs.x);
        Shortcuts(a);

        const float top = 76, footer = 34, gap = 10;
        const float bodyH = vs.y - top - footer - gap;
        const float leftW = std::clamp(vs.x * 0.24f, 300.0f, 380.0f), rightW = std::clamp(vs.x * 0.27f, 330.0f, 440.0f);
        ImGui::SetCursorPos(ImVec2(10, top));
        DrawLeft(a, bodyH, leftW);

        const float midX = 10 + leftW + gap;
        const float midW = vs.x - leftW - rightW - 3 * gap - 10;
        ImGui::SetCursorPos(ImVec2(midX, top));
        DrawCentre(a, ImVec2(midW, bodyH));

        const float rightX = vs.x - rightW - 10;
        const float previewH = std::min(bodyH * 0.6f, rightW + 90);
        ImGui::SetCursorPos(ImVec2(rightX, top));
        DrawPreview(a.render3dTex.t, a.bodyPreviewTex.t, a.hasGuide ? a.guideTex.t : nullptr,
                    ImVec2(rightW, previewH), colors, &a.dollView, &a.yaw, &a.pitch, &a.zoom, &a.inspect);
        DrawProperties(a, ImVec2(rightX, top + previewH + gap), ImVec2(rightW, bodyH - previewH - gap));
        DrawDialogs(a, vs);

        if (a.inspect) {
            if (!ImGui::IsPopupOpen("##inspect")) ImGui::OpenPopup("##inspect");
            ImGui::SetNextWindowSize(ImVec2(vs.x * 0.86f, vs.y * 0.9f));
            ImGui::SetNextWindowPos(ImVec2(vs.x * 0.07f, vs.y * 0.05f));
        }
        if (ImGui::BeginPopupModal("##inspect", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove)) {
            ImGui::TextUnformatted(Tr("Inspeccionar el muñeco", "Inspect the figure"));
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Frente", "Front"))) { a.iyaw = 0; a.ipitch = 0.08f; }
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Espalda", "Back"))) { a.iyaw = 3.14159f; a.ipitch = 0.08f; }
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Izquierda", "Left"))) { a.iyaw = 1.5708f; a.ipitch = 0.08f; }
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Derecha", "Right"))) { a.iyaw = -1.5708f; a.ipitch = 0.08f; }
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Desde arriba", "From above"))) a.ipitch = 0.9f;
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Restablecer", "Reset"))) { a.iyaw = 0; a.ipitch = 0.08f; a.izoom = 1; a.ipanX = 0; a.icenterY = 4.0f; }
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Cerrar", "Close")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                a.inspect = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextDisabled("%s", Tr("Arrastra: girar · rueda: zoom · botón derecho: mover · también las flechas",
                                         "Drag: rotate · wheel: zoom · right button: move · arrows too"));
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const int side = std::clamp(static_cast<int>(std::min(avail.x, avail.y)), 200, 1000);
            // Rendered at the size it is shown, so zooming in stays sharp.
            Image body = Compose(a.canvases[0]);
            if (a.piecesOn) DrawNumber(body, a.gameBase, a.pieces);
            Image capImg = Compose(a.canvases[1]);
            bool capUsed = false;
            for (size_t i = 3; i < capImg.rgba.size(); i += 4) capUsed |= capImg.rgba[i] >= 128;
            if (capUsed) FillBehind(capImg, CapFill(a, body));
            std::vector<const Image*> extras{&capImg};
            for (const Image& t : a.doll.textures) extras.push_back(&t);
            Upload(renderer, a.inspectTex,
                   a.doll.ok ? RenderMesh(a.doll.mesh, body, side, side, a.iyaw, a.ipitch, a.izoom, a.icenterY, &extras, a.ipanX)
                             : RenderMesh(a.mesh, body, side, side, a.iyaw, a.ipitch, a.izoom, a.icenterY, nullptr, a.ipanX));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - side) * 0.5f);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + side, at.y + side), power_pros_ui::Rgba(14, 22, 38), 8);
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(a.inspectTex.t)), ImVec2(side, side));
            ImGui::SetCursorScreenPos(at);
            ImGui::InvisibleButton("##ispin", ImVec2(side, side), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                a.iyaw += io.MouseDelta.x * 0.01f;
                a.ipitch = std::clamp(a.ipitch + io.MouseDelta.y * 0.01f, -1.4f, 1.4f);
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                const float unit = 12.0f / std::max(0.2f, a.izoom) / (side * 0.95f);  // screen px -> model units
                a.ipanX -= io.MouseDelta.x * unit;
                a.icenterY += io.MouseDelta.y * unit;
            }
            if (ImGui::IsItemHovered() && io.MouseWheel != 0)
                a.izoom = std::clamp(a.izoom * (io.MouseWheel > 0 ? 1.15f : 1.0f / 1.15f), 0.4f, 8.0f);
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) a.iyaw -= 0.03f;
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) a.iyaw += 0.03f;
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) a.ipitch = std::max(-1.4f, a.ipitch - 0.03f);
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) a.ipitch = std::min(1.4f, a.ipitch + 0.03f);
            ImGui::EndPopup();
        }

        if (!a.toast.empty())
            power_pros_ui::ShadowedText(ImGui::GetWindowDrawList(), power_pros_ui::g_bold, 16,
                                        ImVec2(16, vs.y - footer + 6), a.toastError ? ui::kRedTop : ui::kMuted,
                                        a.toast.c_str());
        ImGui::End();
        TrackHistory(a);
        if (undoTest > 0) {  // --undo-selftest: change a colour, let it settle, undo, redo
            const int f = ++undoTest;
            if (f == 4) a.pieces.color[kCap] = (a.pieces.color[kCap] + 5) % 32;
            if (f == 8) Undo(a);
            if (f == 10) undoOk = a.pieces.color[kCap] == 21 && a.redo.size() == 1;
            if (f == 12) Redo(a);
            if (f == 14) {
                undoOk = undoOk && a.pieces.color[kCap] == 26 && a.undo.size() == 1;
                std::printf("%s\n", undoOk ? "UNDO OK" : "UNDO FAIL");
                quit = true;
            }
        }

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
