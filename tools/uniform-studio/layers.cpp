// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers.h"

#include "images.h"  // tstudio::LoadImage / SavePng
#include "render3d.h"  // the thumbnail: the preview mannequin, front view
#include "lang.h"

#include <imgui.h>

#if defined(_WIN32)
#include <windows.h>
#undef LoadImage  // windows.h macro; tstudio::LoadImage is ours
#else
#include <spawn.h>
#include <sys/wait.h>
#include <cstring>
extern char** environ;
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

namespace ustudio {
namespace fs = std::filesystem;
using tstudio::Image;

namespace {
const KindInfo kInfo[kKindCount] = {{"body", 128, 128}, {"cap", 64, 32}, {"helmet", 64, 32}, {"number", 64, 32}};

void Over(Image& d, int x, int y, int r, int g, int b, int a) {
    if (x < 0 || y < 0 || x >= static_cast<int>(d.width) || y >= static_cast<int>(d.height) || a <= 0) return;
    const size_t i = (static_cast<size_t>(y) * d.width + x) * 4;
    const float af = a / 255.0f, ia = 1.0f - af;
    d.rgba[i] = static_cast<uint8_t>(r * af + d.rgba[i] * ia);
    d.rgba[i + 1] = static_cast<uint8_t>(g * af + d.rgba[i + 1] * ia);
    d.rgba[i + 2] = static_cast<uint8_t>(b * af + d.rgba[i + 2] * ia);
    d.rgba[i + 3] = static_cast<uint8_t>(a + d.rgba[i + 3] * ia);
}

// Resample to dw x dh. Shrinking (per axis): exact area, each output pixel the coverage-weighted mean of
// the source it spans (fractional edges count by how much of them it covers), so a logo shrunk by any
// factor, or squeezed in x or y only, keeps its thin lines evenly instead of dropping some. Enlarging:
// nearest, crisp. Alpha-weighted; premultiplied float RGBA out. The layer's source is never changed, so
// resizing again always starts from the original picture.
std::vector<float> ResampleArea(const Image& s, int dw, int dh) {
    struct Tap { int i; float w; };
    auto taps = [](int src, int dst) {
        std::vector<std::vector<Tap>> t(static_cast<size_t>(dst));
        const double f = static_cast<double>(src) / dst;
        for (int o = 0; o < dst; ++o) {
            if (dst >= src) { t[static_cast<size_t>(o)].push_back({std::min(src - 1, static_cast<int>((o + 0.5) * f)), 1.0f}); continue; }
            const double a = o * f, b = (o + 1) * f;
            for (int i = static_cast<int>(a); i < src && i < b; ++i) {
                const double w = std::min(b, i + 1.0) - std::max(a, static_cast<double>(i));
                if (w > 0) t[static_cast<size_t>(o)].push_back({i, static_cast<float>(w / (b - a))});
            }
        }
        return t;
    };
    const int sw = static_cast<int>(s.width), sh = static_cast<int>(s.height);
    const auto tx = taps(sw, dw), ty = taps(sh, dh);
    std::vector<float> row(static_cast<size_t>(dw) * sh * 4, 0.0f), out(static_cast<size_t>(dw) * dh * 4, 0.0f);
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < dw; ++x) {
            float* o = &row[(static_cast<size_t>(y) * dw + x) * 4];
            for (const Tap& t : tx[static_cast<size_t>(x)]) {
                const uint8_t* p = &s.rgba[(static_cast<size_t>(y) * sw + t.i) * 4];
                const float a = p[3] * t.w;
                o[0] += p[0] * a; o[1] += p[1] * a; o[2] += p[2] * a; o[3] += a;
            }
        }
    for (int y = 0; y < dh; ++y)
        for (const Tap& t : ty[static_cast<size_t>(y)])
            for (int x = 0; x < dw; ++x) {
                const float* r = &row[(static_cast<size_t>(t.i) * dw + x) * 4];
                float* o = &out[(static_cast<size_t>(y) * dw + x) * 4];
                for (int k = 0; k < 4; ++k) o[k] += r[k] * t.w;
            }
    return out;
}

void OverPremul(Image& d, int x, int y, const float* p) {
    if (p[3] < 0.5f) return;
    Over(d, x, y, static_cast<int>(p[0] / p[3] + 0.5f), static_cast<int>(p[1] / p[3] + 0.5f),
         static_cast<int>(p[2] / p[3] + 0.5f), static_cast<int>(p[3] + 0.5f));
}

void BlitScaled(Image& out, const Layer& L) {
    if (L.src.Empty()) return;
    const int dw = std::max(1, static_cast<int>(std::lround(L.DrawnW())));
    const int dh = std::max(1, static_cast<int>(std::lround(L.DrawnH())));
    const std::vector<float> img = ResampleArea(L.src, dw, dh);
    const float turn = std::remainder(L.rot, 360.0f);
    if (std::fabs(turn) < 0.01f) {
        const int ox = static_cast<int>(std::lround(L.x)), oy = static_cast<int>(std::lround(L.y));
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) OverPremul(out, ox + x, oy + y, &img[(static_cast<size_t>(y) * dw + x) * 4]);
        return;
    }
    // Turned about the box centre: each atlas pixel averages 4 x 4 points mapped back into the resized
    // picture (smooth edges at any angle).
    constexpr int kSub = 4;
    const float r = turn * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
    const float cx = L.x + dw / 2.0f, cy = L.y + dh / 2.0f;
    const float ex = (std::fabs(c) * dw + std::fabs(s) * dh) / 2, ey = (std::fabs(s) * dw + std::fabs(c) * dh) / 2;
    for (int py = static_cast<int>(std::floor(cy - ey)); py <= static_cast<int>(std::ceil(cy + ey)); ++py)
        for (int px = static_cast<int>(std::floor(cx - ex)); px <= static_cast<int>(std::ceil(cx + ex)); ++px) {
            float acc[4] = {};
            for (int j = 0; j < kSub; ++j)
                for (int i = 0; i < kSub; ++i) {
                    const float dx = px + (i + 0.5f) / kSub - cx, dy = py + (j + 0.5f) / kSub - cy;
                    const int lx = static_cast<int>(std::floor(c * dx + s * dy + dw / 2.0f));
                    const int ly = static_cast<int>(std::floor(-s * dx + c * dy + dh / 2.0f));
                    if (lx < 0 || ly < 0 || lx >= dw || ly >= dh) continue;
                    const float* p = &img[(static_cast<size_t>(ly) * dw + lx) * 4];
                    for (int k = 0; k < 4; ++k) acc[k] += p[k];
                }
            for (float& v : acc) v /= kSub * kSub;
            OverPremul(out, px, py, acc);
        }
}

// Quote one argument for a Windows command line / keep it plain for posix_spawn (passed as argv).
std::string NarrowPath(const fs::path& p) { return tstudio::Utf8(p); }

// Run the interpreter with one job file; stdout+stderr go to <tempDir>/bridge.log, read back on failure.
int RunPython(const fs::path& pyExe, const fs::path& script, const fs::path& jobFile, const fs::path& logFile,
              std::string* output) {
#if defined(_WIN32)
    std::wstring cmd = L"\"" + pyExe.wstring() + L"\" -I \"" + script.wstring() + L"\" \"" + jobFile.wstring() + L"\"";
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE log = CreateFileW(logFile.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = log;
    si.hStdError = log;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (log) CloseHandle(log);
    if (!ok) { if (output) *output = "no se pudo iniciar Python"; return -1; }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#else
    const std::string py = pyExe.string(), sc = script.string(), jb = jobFile.string(), lg = logFile.string();
    std::string sh = "\"" + py + "\" -I \"" + sc + "\" \"" + jb + "\" > \"" + lg + "\" 2>&1";
    const int rc = std::system(sh.c_str());
    const int code = rc < 0 ? -1 : WEXITSTATUS(rc);
#endif
    if (output) {
        std::ifstream f(logFile, std::ios::binary);
        *output = std::string((std::istreambuf_iterator<char>(f)), {});
    }
    return static_cast<int>(code);
}

std::string JsonString(const fs::path& p) {
    std::string out = "\"";
    for (char c : NarrowPath(p)) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out + "\"";
}
} // namespace

const KindInfo& Info(Kind k) { return kInfo[static_cast<int>(k)]; }

void FillBehind(Image& img, const std::array<uint8_t, 3>& rgb) {
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        const float a = img.rgba[i + 3] / 255.0f;
        for (int k = 0; k < 3; ++k) img.rgba[i + k] = static_cast<uint8_t>(img.rgba[i + k] * a + rgb[k] * (1 - a));
        img.rgba[i + 3] = 255;
    }
}

float Layer::DrawnW() const { return isText ? 0.0f : src.width * scale * sx; }
float Layer::DrawnH() const { return isText ? 0.0f : src.height * scale * sy; }

void RasterText(Image& dst, ImFont* font, const Layer& L) {
    if (!font || L.text.empty()) return;
    unsigned char* atlas = nullptr;
    int aw = 0, ah = 0;
    ImGui::GetIO().Fonts->GetTexDataAsAlpha8(&atlas, &aw, &ah);
    if (!atlas) return;
    const float scale = L.fontPx / font->FontSize;
    const float baseline = L.y + L.fontPx * 0.82f;  // top-of-box placement, approx ascent
    float penX = L.x;
    for (size_t i = 0; i < L.text.size(); ++i) {
        const auto c = static_cast<unsigned char>(L.text[i]);
        const ImFontGlyph* g = font->FindGlyph(static_cast<ImWchar>(c));
        if (!g) continue;
        if (g->Visible) {
            const int sx0 = static_cast<int>(g->U0 * aw), sy0 = static_cast<int>(g->V0 * ah);
            const int gw = static_cast<int>((g->U1 - g->U0) * aw), gh = static_cast<int>((g->V1 - g->V0) * ah);
            const int dw = std::max(1, static_cast<int>(std::lround(gw * scale)));
            const int dh = std::max(1, static_cast<int>(std::lround(gh * scale)));
            const float dx0 = penX + g->X0 * scale, dy0 = baseline + g->Y0 * scale;
            for (int dy = 0; dy < dh; ++dy) {
                const int sy = sy0 + std::min(gh - 1, dy * gh / dh);
                for (int dx = 0; dx < dw; ++dx) {
                    const int sx = sx0 + std::min(gw - 1, dx * gw / dw);
                    const int cov = atlas[static_cast<size_t>(sy) * aw + sx];
                    if (cov <= 0) continue;
                    Over(dst, static_cast<int>(std::lround(dx0)) + dx, static_cast<int>(std::lround(dy0)) + dy,
                         L.color[0], L.color[1], L.color[2], cov * L.color[3] / 255);
                }
            }
        }
        penX += g->AdvanceX * scale;
    }
}

Image BuildMould(Kind k) {
    if (k != Kind::Body) return {};  // only the 128x128 body atlas has a mould guide
    const int W = 128, H = 128;
    Image out{static_cast<uint32_t>(W), static_cast<uint32_t>(H), std::vector<uint8_t>(static_cast<size_t>(W) * H * 4, 0)};
    auto fill = [&](int x0, int y0, int x1, int y1, int r, int g, int b, int alpha) {
        for (int y = std::max(0, y0); y < std::min(H, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(W, x1); ++x) {
                uint8_t* p = &out.rgba[(static_cast<size_t>(y) * W + x) * 4];
                p[0] = static_cast<uint8_t>(r); p[1] = static_cast<uint8_t>(g); p[2] = static_cast<uint8_t>(b);
                p[3] = static_cast<uint8_t>(alpha);
            }
    };
    // Schematic zones (authored approximation of the real atlas layout, same colour scheme as the analysis
    // map): jersey chest+back, trousers, sleeves/belt/socks, cap crown, cap brim.
    fill(0, 0, 84, 60, 230, 230, 230, 150);    // jersey (chest / back)
    fill(0, 60, 56, 110, 80, 80, 220, 150);    // trousers
    fill(0, 110, 84, 128, 220, 60, 60, 150);   // sleeves / belt / socks
    fill(86, 0, 128, 64, 60, 200, 60, 150);    // cap crown
    fill(118, 0, 128, 64, 240, 200, 0, 170);   // cap brim
    return out;
}

Image BuildTemplate(Kind k, Template t) {
    if (t == Template::None) return {};
    Image m = BuildMould(k);
    if (m.Empty()) return {};
    Image out{m.width, m.height, std::vector<uint8_t>(m.rgba.size(), 0)};
    const uint8_t white[3] = {235, 235, 235}, navy[3] = {28, 38, 86}, accent[3] = {170, 30, 42};
    for (uint32_t y = 0; y < m.height; ++y)
        for (uint32_t x = 0; x < m.width; ++x) {
            const size_t i = (static_cast<size_t>(y) * m.width + x) * 4;
            if (m.rgba[i + 3] == 0) continue;  // outside the body regions
            const int mr = m.rgba[i], mg = m.rgba[i + 1], mb = m.rgba[i + 2];
            const bool chest = mr > 200 && mg > 200 && mb > 200;     // jersey
            const bool trousers = mb > 180 && mr < 150;              // blue zone
            const uint8_t* c = white;
            switch (t) {
            case Template::Plain: c = white; break;
            case Template::Pinstripes: c = ((chest || trousers) && x % 6 == 0) ? navy : white; break;
            case Template::TwoTone: c = trousers ? white : navy; break;
            case Template::Placket: c = (chest && x >= 9 && x <= 13) ? accent : white; break;
            case Template::None: break;
            }
            out.rgba[i] = c[0];
            out.rgba[i + 1] = c[1];
            out.rgba[i + 2] = c[2];
            out.rgba[i + 3] = 255;
        }
    return out;
}

Image Compose(const Canvas& c) {
    const KindInfo& in = Info(c.kind);
    Image out{static_cast<uint32_t>(in.w), static_cast<uint32_t>(in.h),
              std::vector<uint8_t>(static_cast<size_t>(in.w) * in.h * 4, 0)};
    for (const Layer& L : c.layers) {
        if (!L.visible) continue;
        if (L.isText) RasterText(out, power_pros_ui::g_bold, L);
        else BlitScaled(out, L);
    }
    return out;
}

int CountColors(const Image& img) {
    std::set<uint32_t> seen;
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        if (img.rgba[i + 3] < 0x80) continue;
        seen.insert(static_cast<uint32_t>(img.rgba[i]) << 16 | img.rgba[i + 1] << 8 | img.rgba[i + 2]);
        if (seen.size() > 100000) break;
    }
    return static_cast<int>(seen.size());
}

BridgeResult SaveUniform(const fs::path& pyExe, const fs::path& toolDir, const fs::path& tempDir, const fs::path& out,
                         const std::array<Canvas, kKindCount>& canvases, const std::array<int, 10>& backup,
                         const GameDoll* doll, const std::array<uint8_t, 3>* capFill, int flags,
                         const std::array<int, 6>* numberLayout) {
    std::error_code ec;
    fs::create_directories(tempDir, ec);
    fs::create_directories(out.parent_path(), ec);
    std::string atlasJson;
    for (int i = 0; i < kKindCount; ++i) {
        Image img = Compose(canvases[i]);
        bool any = false;
        for (size_t p = 3; p < img.rgba.size(); p += 4)
            if (img.rgba[p] >= 0x80) { any = true; break; }
        if (!any) continue;  // skip empty atlases
        if (capFill && (canvases[i].kind == Kind::Cap || canvases[i].kind == Kind::Helmet)) FillBehind(img, *capFill);
        const fs::path png = tempDir / (std::string(Info(canvases[i].kind).id) + ".png");
        std::string err;
        if (!tstudio::SavePng(png, img, &err)) return {false, err};
        atlasJson += (atlasJson.empty() ? "" : ",") + ("\"" + std::string(Info(canvases[i].kind).id) + "\":") + JsonString(png);
    }
    if (atlasJson.empty())
        return {false, tstudio::Tr("No hay nada que guardar: todas las capas están vacías.",
                                   "Nothing to save: every atlas is empty.")};
    // The thumbnail the game's LINE TYPE window shows for this uniform (as the game's own pictures show
    // the jersey): the preview mannequin's torso and arms wearing the body atlas, front view, framed on
    // the torso (centre y 4.6, zoom 2.3: torso, arms and shoulders), rendered at 4x and box-filtered down to 64 x 64.
    if (atlasJson.find("\"body\"") != std::string::npos) {
        constexpr int kThumb = 64, kScale = 4;
        // With the game's own figure (read from the player's disc): the whole chibi, as the game shows it.
        const Image body = Compose(canvases[0]), cap = Compose(canvases[1]);
        std::vector<const Image*> extras{&cap};
        if (doll && doll->ok)
            for (const Image& t : doll->textures) extras.push_back(&t);
        const Image big = doll && doll->ok
                              ? RenderMesh(doll->mesh, body, kThumb * kScale, kThumb * kScale, 0.0f, 0.08f, 1.2f, 3.9f, &extras)
                              : RenderMesh(BuildMannequin(), body, kThumb * kScale, kThumb * kScale, 0.0f, 0.08f, 2.3f, 4.6f);
        Image thumb{kThumb, kThumb, std::vector<uint8_t>(size_t(kThumb) * kThumb * 4, 0)};
        for (int y = 0; y < kThumb; ++y)
            for (int x = 0; x < kThumb; ++x) {
                int sum[4] = {}, n = 0;
                for (int dy = 0; dy < kScale; ++dy)
                    for (int dx = 0; dx < kScale; ++dx) {
                        const uint8_t* q = &big.rgba[(size_t(y * kScale + dy) * big.width + x * kScale + dx) * 4];
                        if (q[3] < 0x80) continue;
                        for (int c = 0; c < 4; ++c) sum[c] += q[c];
                        ++n;
                    }
                if (n * 2 < kScale * kScale) continue;  // mostly background: stays transparent
                uint8_t* o = &thumb.rgba[(size_t(y) * kThumb + x) * 4];
                for (int c = 0; c < 3; ++c) o[c] = uint8_t(sum[c] / n);
                o[3] = 255;
            }
        const fs::path png = tempDir / "thumb.png";
        std::string err;
        if (!tstudio::SavePng(png, thumb, &err)) return {false, err};
        atlasJson += ",\"thumb\":" + JsonString(png);
    }
    std::string backupJson;
    for (int v : backup) backupJson += (backupJson.empty() ? "" : ",") + std::to_string(std::clamp(v, 0, 31));
    const fs::path job = tempDir / "job.json";
    {
        std::ofstream f(job, std::ios::binary);
        std::string layoutJson;
        if (numberLayout)
            for (int v : *numberLayout) layoutJson += (layoutJson.empty() ? "" : ",") + std::to_string(v);
        f << "{\"op\":\"bake\",\"flags\":" << flags << (layoutJson.empty() ? "" : ",\"layout\":[" + layoutJson + "]")
          << ",\"out\":" << JsonString(out) << ",\"backup\":[" << backupJson
          << "],\"atlases\":{" << atlasJson << "}}";
    }
    std::string log;
    const int code = RunPython(pyExe, toolDir / "studio_bridge.py", job, tempDir / "bridge.log", &log);
    if (code != 0) return {false, (log.empty() ? tstudio::Tr("falló el horneado", "bake failed") : log)};
    return {true, NarrowPath(out)};
}

BridgeResult ImportSource(const fs::path& pyExe, const fs::path& toolDir, const fs::path& tempDir, const fs::path& file,
                          std::vector<Layer>* out) {
    std::string ext = tstudio::Utf8(file.extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".png" || ext == ".dds") {  // read directly, no Python needed
        std::string err;
        auto img = tstudio::LoadImage(file, &err);
        if (!img) return {false, err};
        Layer L;
        L.name = tstudio::Utf8(file.stem());
        L.src = std::move(*img);
        out->push_back(std::move(L));
        return {true, ""};
    }
    std::error_code ec;
    const fs::path dir = tempDir / "import";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path job = tempDir / "job.json";
    {
        std::ofstream f(job, std::ios::binary);
        f << "{\"op\":\"import\",\"src\":" << JsonString(file) << ",\"outdir\":" << JsonString(dir) << "}";
    }
    std::string log;
    const int code = RunPython(pyExe, toolDir / "studio_bridge.py", job, tempDir / "bridge.log", &log);
    if (code != 0) return {false, (log.empty() ? tstudio::Tr("no se pudo importar", "import failed") : log)};
    std::ifstream index(dir / "index.txt", std::ios::binary);
    std::string line;
    int loaded = 0;
    while (std::getline(index, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        const std::string fname = line.substr(0, tab);
        if (fname.empty()) continue;
        std::string err;
        auto img = tstudio::LoadImage(dir / fname, &err);
        if (!img) continue;
        Layer L;
        L.name = tab == std::string::npos ? fname : (fname + "  [" + line.substr(tab + 1) + "]");
        L.src = std::move(*img);
        out->push_back(std::move(L));
        ++loaded;
    }
    if (loaded == 0)
        return {false, tstudio::Tr("El archivo no contiene capas reconocibles.",
                                   "The file has no recognisable layers.")};
    return {true, ""};
}

BridgeResult LoadGamePack(const fs::path& pyExe, const fs::path& toolDir, const fs::path& tempDir,
                          const fs::path& pack, const fs::path& common, Layer* out) {
    std::error_code ec;
    const fs::path dir = tempDir / "pack";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path job = tempDir / "job.json";
    {
        std::ofstream f(job, std::ios::binary);
        f << "{\"op\":\"pack\",\"pack\":" << JsonString(pack) << ",\"common\":" << JsonString(common)
          << ",\"outdir\":" << JsonString(dir) << "}";
    }
    std::string log;
    const int code = RunPython(pyExe, toolDir / "studio_bridge.py", job, tempDir / "bridge.log", &log);
    if (code != 0) return {false, (log.empty() ? tstudio::Tr("no se pudo leer el pack", "could not read the pack") : log)};
    std::string err;
    auto img = tstudio::LoadImage(dir / "uniform_from_pack.png", &err);
    if (!img) return {false, err};
    out->name = tstudio::Utf8(pack.stem());
    out->src = std::move(*img);
    return {true, ""};
}

BridgeResult ExtractFromGame(const fs::path& pyExe, const fs::path& toolDir, const fs::path& tempDir, const char* op,
                            const fs::path& projectBin, const fs::path& out) {
    std::error_code ec;
    fs::create_directories(tempDir, ec);
    const fs::path job = tempDir / "job.json";
    {
        std::ofstream f(job, std::ios::binary);
        f << "{\"op\":\"" << op << "\",\"project\":" << JsonString(projectBin) << ",\"out\":" << JsonString(out) << "}";
    }
    std::string log;
    const int code = RunPython(pyExe, toolDir / "studio_bridge.py", job, tempDir / "bridge.log", &log);
    if (code != 0) return {false, log.empty() ? tstudio::Tr("no se pudo leer el juego", "could not read the game") : log};
    return {true, ""};
}

BridgeResult ReadPpu(const fs::path& pyExe, const fs::path& toolDir, const fs::path& tempDir, const fs::path& ppu,
                     const fs::path& outdir) {
    std::error_code ec;
    fs::remove_all(outdir, ec);
    fs::create_directories(outdir, ec);
    const fs::path job = tempDir / "job.json";
    {
        std::ofstream f(job, std::ios::binary);
        f << "{\"op\":\"readppu\",\"ppu\":" << JsonString(ppu) << ",\"outdir\":" << JsonString(outdir) << "}";
    }
    std::string log;
    const int code = RunPython(pyExe, toolDir / "studio_bridge.py", job, tempDir / "bridge.log", &log);
    if (code != 0) return {false, log.empty() ? tstudio::Tr("no se pudo leer el uniforme", "could not read the uniform") : log};
    return {true, ""};
}

} // namespace ustudio
