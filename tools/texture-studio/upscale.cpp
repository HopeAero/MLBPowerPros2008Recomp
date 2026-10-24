// SPDX-License-Identifier: GPL-3.0-or-later
#include "upscale.h"

#include "images.h"
#include "lang.h"

#if defined(_WIN32)
#include <windows.h>
#undef LoadImage  // windows.h macro; tstudio::LoadImage is ours
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include <algorithm>
#include <chrono>
#include <functional>
#include <set>
#include <thread>

namespace tstudio {
namespace {
namespace fs = std::filesystem;

#if defined(_WIN32)
constexpr const char* kEngineZip =
    "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip";
constexpr const char* kEngineExe = "realesrgan-ncnn-vulkan.exe";
#else
constexpr const char* kEngineZip =
    "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-ubuntu.zip";
constexpr const char* kEngineExe = "realesrgan-ncnn-vulkan";
#endif
constexpr const char* kAnimeSharp = "4x-AnimeSharp-fp16";
constexpr const char* kAnimeSharpUrl = "https://huggingface.co/Kim2091/AnimeSharp/resolve/main/NCNN/";
constexpr uint32_t kMaxSide = 4096;

fs::path Engine(const fs::path& tools) { return tools / kEngineExe; }
fs::path Models(const fs::path& tools) { return tools / "models"; }
bool HasModel(const fs::path& tools, const std::string& id) {
    std::error_code ec;
    return fs::exists(Models(tools) / (id + ".param"), ec) && fs::exists(Models(tools) / (id + ".bin"), ec);
}

// Runs a program without a window and waits for it; `watch` is called every 200 ms meanwhile.
int Run(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd,
        const std::function<void()>& watch = {}) {
#if defined(_WIN32)
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    for (const auto& a : args) command += L" \"" + PathFromUtf8(a).wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi))
        return -1;
    CloseHandle(pi.hThread);
    while (WaitForSingleObject(pi.hProcess, 200) == WAIT_TIMEOUT)
        if (watch) watch();
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#else
    std::vector<std::string> all{exe.string()};
    all.insert(all.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : all) argv.push_back(s.data());
    argv.push_back(nullptr);
    std::error_code ec;
    const auto previous = fs::current_path(ec);
    if (!cwd.empty()) fs::current_path(cwd, ec);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
    fs::current_path(previous, ec);
    if (rc != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, WNOHANG) == 0) {
        if (watch) watch();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

// The system's own curl (Windows 10 and later, every Linux): -L follows redirects, --fail on HTTP errors.
bool Fetch(const std::string& url, const fs::path& file, std::string* error) {
#if defined(_WIN32)
    const fs::path curl = "curl.exe";
#else
    const fs::path curl = "curl";
#endif
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path part = fs::path(file).concat(".part");
    if (Run(curl, {"-L", "--fail", "-s", "-o", Utf8(part), url}, {}) != 0 || !fs::exists(part, ec)) {
        if (error) *error = Tr("No se pudo descargar ", "Could not download ") + url;
        fs::remove(part, ec);
        return false;
    }
    fs::rename(part, file, ec);
    return !ec;
}
} // namespace

bool UnpackZip(const fs::path& zip, const fs::path& dir, std::string* error) {
    std::error_code ec;
    fs::create_directories(dir, ec);
#if defined(_WIN32)
    const int rc = Run("tar.exe", {"-xf", Utf8(zip), "-C", Utf8(dir)}, {});
#else
    const int rc = Run("unzip", {"-o", "-q", Utf8(zip), "-d", Utf8(dir)}, {});
#endif
    if (rc != 0 && error) *error = Tr("No se pudo descomprimir ", "Could not unpack ") + Utf8(zip.filename());
    return rc == 0;
}

std::vector<UpscaleModel> UpscaleModels(const fs::path& tools) {
    std::error_code ec;
    const bool engine = fs::exists(Engine(tools), ec);
    std::vector<UpscaleModel> out{
        {kAnimeSharp, Tr("AnimeSharp (recomendado: dibujos, letras, menús)", "AnimeSharp (recommended: art, lettering, menus)")},
        {"realesrgan-x4plus-anime", Tr("Real-ESRGAN anime (más nítido, contornos marcados)", "Real-ESRGAN anime (sharper, strong outlines)")},
        {"realesrgan-x4plus", Tr("Real-ESRGAN x4plus (fotos: estadios, cartas)", "Real-ESRGAN x4plus (photos: stadiums, cards)")}};
    std::set<std::string> known;
    for (const auto& m : out) known.insert(m.id);
    for (const auto& e : fs::directory_iterator(Models(tools), ec)) {
        if (e.path().extension() != ".param") continue;
        const std::string id = Utf8(e.path().stem());
        if (!known.count(id) && HasModel(tools, id)) out.push_back({id, id + Tr(" (tu modelo)", " (your model)")});
    }
    for (auto& m : out) m.ready = engine && HasModel(tools, m.id);
    return out;
}

std::string UpscaleDownloads(const fs::path& tools, const std::string& model) {
    std::error_code ec;
    std::string text;
    if (!fs::exists(Engine(tools), ec))
        text += Tr("- Real-ESRGAN ncnn-vulkan (unos 45 MB, licencia MIT; GitHub de xinntao)\n",
                   "- Real-ESRGAN ncnn-vulkan (about 45 MB, MIT license; xinntao's GitHub)\n");
    if (model == kAnimeSharp && !HasModel(tools, model))
        text += Tr("- 4x-AnimeSharp de Kim2091 (unos 33 MB, licencia CC BY-NC-SA 4.0: uso no comercial; Hugging Face)\n",
                   "- 4x-AnimeSharp by Kim2091 (about 33 MB, CC BY-NC-SA 4.0 license: non-commercial use; Hugging Face)\n");
    return text;
}

bool DownloadUpscaler(const fs::path& tools, const std::string& model, std::string* error) {
    std::error_code ec;
    if (!fs::exists(Engine(tools), ec)) {
        const fs::path zip = tools / "engine.zip";
        if (!Fetch(kEngineZip, zip, error)) return false;
        const bool unpacked = UnpackZip(zip, tools, nullptr);
        fs::remove(zip, ec);
        for (const char* demo : {"input.jpg", "input2.jpg", "onepiece_demo.mp4"}) fs::remove(tools / demo, ec);  // its samples
        // Some zips keep everything in one folder: bring its files up.
        if (!fs::exists(Engine(tools), ec))
            for (const auto& e : fs::directory_iterator(tools, ec))
                if (e.is_directory() && fs::exists(e.path() / kEngineExe))
                    for (const auto& f : fs::directory_iterator(e.path(), ec)) fs::rename(f.path(), tools / f.path().filename(), ec);
        if (!unpacked || !fs::exists(Engine(tools), ec)) {
            if (error) *error = Tr("No se pudo descomprimir Real-ESRGAN.", "Could not unpack Real-ESRGAN.");
            return false;
        }
#if !defined(_WIN32)
        fs::permissions(Engine(tools), fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
#endif
    }
    if (model == kAnimeSharp && !HasModel(tools, model))
        for (const char* ext : {".param", ".bin"})
            if (!Fetch(std::string(kAnimeSharpUrl) + kAnimeSharp + ext, Models(tools) / (std::string(kAnimeSharp) + ext), error))
                return false;
    if (!HasModel(tools, model)) {
        if (error) *error = Tr("Falta el modelo ", "Missing model ") + model;
        return false;
    }
    return true;
}

bool Upscale(const fs::path& tools, const std::string& model, int scale, const fs::path& in, const fs::path& out,
             Progress* progress, std::string* error) {
    std::error_code ec;
    // The engine reads one flat folder: the images are gathered there under their (unique) tex1_ names.
    const fs::path flat = out.parent_path() / "upscale-in", raw = out.parent_path() / "upscale-raw";
    fs::remove_all(flat, ec), fs::remove_all(raw, ec);
    fs::create_directories(flat, ec), fs::create_directories(raw, ec), fs::create_directories(out, ec);
    size_t total = 0;
    for (fs::recursive_directory_iterator it(in, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file() && it->path().extension() == ".png" && !fs::exists(flat / it->path().filename()))
            fs::copy_file(it->path(), flat / it->path().filename(), ec), ++total;
    if (progress) progress->total = total * 2;  // upscaling, then sizing
    if (total == 0) {
        if (error) *error = Tr("No hay texturas que escalar en esta vista.", "There are no textures to upscale in this view.");
        return false;
    }
    // Tiles of 128 keep video memory low on 4 GB cards (the whole image at once fails on large textures).
    const auto count = [&] {
        size_t n = 0;
        for (const auto& e : fs::directory_iterator(raw, ec)) n += e.is_regular_file();
        if (progress) progress->done = n;
    };
    const int rc = Run(Engine(tools), {"-i", Utf8(flat), "-o", Utf8(raw), "-n", model, "-s", "4", "-m", Utf8(Models(tools)),
                                       "-t", "128", "-f", "png"},
                       tools, count);
    if (rc != 0) {
        if (error) *error = Tr("Real-ESRGAN falló (¿tarjeta sin Vulkan?). Código ", "Real-ESRGAN failed (no Vulkan card?). Code ") +
                            std::to_string(rc);
        return false;
    }
    size_t done = 0;
    for (const auto& e : fs::directory_iterator(flat, ec)) {
        std::string problem;
        const auto original = LoadImage(e.path(), &problem);
        const auto big = LoadImage(raw / e.path().filename(), &problem);
        if (progress) progress->done = total + ++done;
        if (!original || !big) continue;
        // 4x, halved for 2x; the largest power-of-two factor up to `scale` that keeps 4096 or less.
        uint32_t factor = static_cast<uint32_t>(scale);
        const uint32_t longer = std::max(original->width, original->height);
        while (factor > 1 && longer * factor > kMaxSide) factor /= 2;
        const Image sized = Downscale(*big, longer * factor);
        if (!SavePng(out / e.path().filename(), sized, &problem) && error) *error = problem;
    }
    fs::remove_all(flat, ec), fs::remove_all(raw, ec);
    return true;
}
} // namespace tstudio
