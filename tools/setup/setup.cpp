// SPDX-License-Identifier: GPL-3.0-or-later
// MLB Power Pros 2008 Setup. The player picks their own disc image; workspace\LocalBuild.ps1 then
// extracts it, translates the game code and compiles the native game on this PC. This program
// ships no game code or assets: it only drives the bundled toolchain and shows progress, in the
// style of the game's own menus (power_pros_ui.h, shared with the in-game launcher).
#include "power_pros_ui.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace power_pros_ui;
namespace fs = std::filesystem;

namespace {

std::wstring Wide(const std::string& s) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), static_cast<int>(w.size()));
    w.pop_back();
    return w;
}

std::string Utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// One run of LocalBuild.ps1 in a hidden PowerShell, inside a job object so cancelling or closing
// the Setup also stops every compiler it started.
struct Build {
    std::mutex mutex;
    int percent = 0;
    std::string message;
    std::string error;
    std::deque<std::string> tail;
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> succeeded{false};
    HANDLE job = nullptr;
    std::thread reader;

    void Start(const fs::path& script, const std::string& iso, const fs::path& install, bool shortcut) {
        Stop();
        percent = 0;
        message = Tr("Preparando...", "Preparing...");
        error.clear();
        tail.clear();
        finished = false;
        succeeded = false;
        fs::create_directories(install);
        std::wstring command = L"powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" +
                               script.wstring() + L"\" -Iso \"" + Wide(iso) + L"\" -Install \"" + install.wstring() +
                               L"\"" + (shortcut ? L" -Shortcut" : L"") +
                               // The script's progress messages come in the Setup's language.
                               (g_lang == Lang::En ? L" -Lang en" : L" -Lang es");
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        CreatePipe(&readPipe, &writePipe, &sa, 0);
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        // PowerShell starts inside the job (PROC_THREAD_ATTRIBUTE_JOB_LIST) rather than suspended and
        // assigned afterwards: CREATE_SUSPENDED + ResumeThread is the process-hollowing pattern that
        // antivirus behaviour rules look for, and a player's Windows Defender quarantined the Setup.
        SIZE_T attributeSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
        std::vector<char> attributes(attributeSize);
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdOutput = writePipe;
        si.StartupInfo.hStdError = writePipe;
        si.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attributeSize);
        UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job, sizeof(job),
                                  nullptr, nullptr);
        PROCESS_INFORMATION pi{};
        const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                            script.parent_path().c_str(), &si.StartupInfo, &pi);
        DeleteProcThreadAttributeList(si.lpAttributeList);
        CloseHandle(writePipe);
        if (!started) {
            CloseHandle(readPipe);
            error = Tr("No se pudo iniciar PowerShell.", "Could not start PowerShell.");
            finished = true;
            return;
        }
        CloseHandle(pi.hThread);
        running = true;
        const fs::path logPath = install / "setup.log";
        reader = std::thread([this, readPipe, process = pi.hProcess, logPath] {
            FILE* log = _wfopen(logPath.c_str(), L"wb");
            std::string pending;
            char buffer[4096];
            DWORD got = 0;
            while (ReadFile(readPipe, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
                if (log != nullptr) fwrite(buffer, 1, got, log);
                pending.append(buffer, got);
                for (size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
                    std::string line = pending.substr(0, nl);
                    pending.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    OnLine(line);
                }
            }
            if (log != nullptr) fclose(log);
            CloseHandle(readPipe);
            WaitForSingleObject(process, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(process, &code);
            CloseHandle(process);
            {
                std::lock_guard lock(mutex);
                if (code != 0 && error.empty()) error = Tr("La instalación falló. Revisa setup.log.", "Installation failed. Check setup.log.");
            }
            succeeded = code == 0;
            running = false;
            finished = true;
        });
    }

    void OnLine(const std::string& line) {
        std::lock_guard lock(mutex);
        if (line.rfind("@@ERROR ", 0) == 0) {
            error = line.substr(8);
        } else if (line.rfind("@@ ", 0) == 0) {
            const size_t space = line.find(' ', 3);
            percent = std::atoi(line.c_str() + 3);
            message = space == std::string::npos ? "" : line.substr(space + 1);
        } else if (!line.empty()) {
            tail.push_back(line.size() > 140 ? line.substr(0, 140) + "..." : line);
            while (tail.size() > 4) tail.pop_front();
        }
    }

    void Stop() {
        if (job != nullptr) {
            TerminateJobObject(job, 1);
            CloseHandle(job);
            job = nullptr;
        }
        if (reader.joinable()) reader.join();
        running = false;
    }
};

struct DialogResult {
    std::mutex mutex;
    std::string path;
    bool ready = false;
};

void SDLCALL OnDialog(void* userdata, const char* const* files, int) {
    auto* result = static_cast<DialogResult*>(userdata);
    std::lock_guard lock(result->mutex);
    result->path = files != nullptr && files[0] != nullptr ? files[0] : "";
    result->ready = true;
}

// Actions are identified by their Spanish label; this is the label shown in the current language.
const char* Label(const char* label) {
    const std::string_view id = label;
    if (id == "JUGAR") return Tr("JUGAR", "PLAY");
    if (id == "ELEGIR DISCO") return Tr("ELEGIR DISCO", "CHOOSE DISC");
    if (id == "INSTALAR") return Tr("INSTALAR", "INSTALL");
    if (id == "REINSTALAR") return Tr("REINSTALAR", "REINSTALL");
    if (id == "CAMBIAR DISCO") return Tr("CAMBIAR DISCO", "CHANGE DISC");
    if (id == "OTRO DISCO") return Tr("OTRO DISCO", "OTHER DISC");
    if (id == "CARPETA") return Tr("CARPETA", "FOLDER");
    if (id == "SALIR") return Tr("SALIR", "EXIT");
    if (id == "CANCELAR") return Tr("CANCELAR", "CANCEL");
    if (id == "REINTENTAR") return Tr("REINTENTAR", "RETRY");
    if (id == "VER REGISTRO") return Tr("VER REGISTRO", "VIEW LOG");
    return label;
}

// Writes the Setup's language to the installed game's Config.toml as [ui] language, so its
// launcher starts in the same language. Line-based: every other line stays as it is; the key goes
// into an existing [ui] section, or a new one at the end. No Config.toml, nothing to do.
void SaveLanguage(const fs::path& config) {
    std::ifstream in(config, std::ios::binary);
    if (!in) return;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const char* eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    for (size_t pos = 0; pos < text.size();) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        pos = nl + 1;
    }
    auto trim = [](std::string_view v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
        return v;
    };
    const std::string entry = std::string("language = \"") + (g_lang == Lang::En ? "en" : "es") + "\"";
    int header = -1;
    bool inUi = false;
    bool done = false;
    for (size_t i = 0; i < lines.size() && !done; ++i) {
        const std::string_view t = trim(lines[i]);
        if (!t.empty() && t.front() == '[') {
            inUi = t.substr(0, t.find(']') + 1) == "[ui]";
            if (inUi) header = static_cast<int>(i);
        } else if (inUi && t.rfind("language", 0) == 0 && trim(t.substr(8)).rfind('=', 0) == 0) {
            lines[i] = entry;
            done = true;
        }
    }
    if (!done && header >= 0) {
        lines.insert(lines.begin() + header + 1, entry);
    } else if (!done) {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.emplace_back();
        lines.emplace_back("[ui]");
        lines.push_back(entry);
    }
    std::string out;
    for (const std::string& line : lines) out += line + eol;
    std::ofstream file(config, std::ios::binary | std::ios::trunc);
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
}

// "ES | EN" switch in the style of the menus: a small blue pill with the current language lit in
// orange. Clicking a half picks that language. Returns the language to use from now on.
Lang LanguagePill(ImDrawList* dl, ImVec2 a, float height, float u, bool click) {
    const float ts = height * 0.56f;
    const float half = std::max(TextSize(g_bold, ts, "ES").x, TextSize(g_bold, ts, "EN").x) + height * 0.8f;
    const ImVec2 b(a.x + half * 2.0f, a.y + height);
    Pill(dl, a, b, Rgba(90, 150, 240), Rgba(30, 80, 190), u * 0.5f);
    Lang picked = g_lang;
    const ImVec2 m = ImGui::GetIO().MousePos;
    for (int i = 0; i < 2; ++i) {
        const Lang lang = i == 0 ? Lang::Es : Lang::En;
        const char* text = i == 0 ? "ES" : "EN";
        const ImVec2 ha(a.x + half * static_cast<float>(i), a.y);
        const ImVec2 hb(ha.x + half, b.y);
        if (lang == g_lang) {
            const float inset = 3 * u;
            const int start = dl->VtxBuffer.Size;
            dl->AddRectFilled(ImVec2(ha.x + inset, ha.y + inset), ImVec2(hb.x - inset, hb.y - inset), IM_COL32_WHITE,
                              (height - 2 * inset) * 0.5f);
            Shade(dl, start, ha.y, hb.y, Rgba(255, 206, 96), Rgba(240, 112, 18));
        }
        const ImVec2 tz = TextSize(g_bold, ts, text);
        ShadowedText(dl, g_bold, ts, ImVec2((ha.x + hb.x - tz.x) * 0.5f, (ha.y + hb.y - tz.y) * 0.5f),
                     lang == g_lang ? IM_COL32_WHITE : Rgba(190, 220, 255), text);
        if (click && m.x >= ha.x && m.x < hb.x && m.y >= ha.y && m.y < hb.y) picked = lang;
    }
    return picked;
}

} // namespace

// A softened copy of the art for the bars of other window shapes: averaged down 24x, box-blurred
// twice and scaled back up to a quarter size with bilinear filtering, all on the CPU so the result
// is smooth whatever filter the renderer uses.
SDL_Surface* BlurredSurface(SDL_Surface* image) {
    SDL_Surface* rgba = SDL_ConvertSurface(image, SDL_PIXELFORMAT_RGBA32);
    if (rgba == nullptr) return nullptr;
    constexpr int kDown = 24;
    const int w = rgba->w, h = rgba->h;
    const int sw = std::max(1, w / kDown), sh = std::max(1, h / kDown);
    std::vector<float> small(static_cast<size_t>(sw) * sh * 4, 0.0f);
    for (int y = 0; y < sh * kDown; ++y) {
        const auto* line = static_cast<const unsigned char*>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch;
        for (int x = 0; x < sw * kDown; ++x) {
            float* d = small.data() + (static_cast<size_t>(y / kDown) * sw + x / kDown) * 4;
            for (int c = 0; c < 4; ++c) d[c] += line[x * 4 + c];
        }
    }
    SDL_DestroySurface(rgba);
    for (float& v : small) v /= static_cast<float>(kDown * kDown);
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> out(small.size());
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                for (int c = 0; c < 4; ++c) {
                    float sum = 0.0f;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            sum += small[(static_cast<size_t>(std::clamp(y + dy, 0, sh - 1)) * sw + std::clamp(x + dx, 0, sw - 1)) * 4 + c];
                        }
                    }
                    out[(static_cast<size_t>(y) * sw + x) * 4 + c] = sum / 9.0f;
                }
            }
        }
        small.swap(out);
    }
    const int ow = std::max(1, w / 4), oh = std::max(1, h / 4);
    SDL_Surface* soft = SDL_CreateSurface(ow, oh, SDL_PIXELFORMAT_RGBA32);
    if (soft == nullptr) return nullptr;
    for (int y = 0; y < oh; ++y) {
        auto* line = static_cast<unsigned char*>(soft->pixels) + static_cast<size_t>(y) * soft->pitch;
        const float fy = std::clamp((y + 0.5f) * sh / oh - 0.5f, 0.0f, static_cast<float>(sh - 1));
        const int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, sh - 1);
        const float ty = fy - static_cast<float>(y0);
        for (int x = 0; x < ow; ++x) {
            const float fx = std::clamp((x + 0.5f) * sw / ow - 0.5f, 0.0f, static_cast<float>(sw - 1));
            const int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, sw - 1);
            const float tx = fx - static_cast<float>(x0);
            for (int c = 0; c < 4; ++c) {
                auto at = [&](int xx, int yy) { return small[(static_cast<size_t>(yy) * sw + xx) * 4 + c]; };
                const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
                const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
                line[x * 4 + c] = static_cast<unsigned char>(std::clamp(top + (bottom - top) * ty, 0.0f, 255.0f));
            }
        }
    }
    return soft;
}

int main(int, char**) {
    SetProcessDPIAware();
    // The language starts as the system's (power_pros_ui::g_lang); MLBPP_SETUP_LANG=es|en (scripted
    // tests) picks one. The ES | EN pill switches it at any time.
    if (const char* lang = std::getenv("MLBPP_SETUP_LANG")) g_lang = LangFromSetting(lang);
    const Lang initialLang = g_lang;
    auto windowTitle = [] { return Tr("MLB Power Pros 2008 - Instalación", "MLB Power Pros 2008 - Setup"); };
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;
    SDL_Window* window = SDL_CreateWindow(windowTitle(), 1280, 720,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    LoadFonts();
    // MoonInning art, shared with the launcher (workspace\launcher\setup.png). Without it the Setup
    // keeps its procedural look.
    SDL_Texture* art = nullptr;
    SDL_Texture* artBlur = nullptr;  // a tiny copy, stretched (blurred) behind the art in other window shapes
    if (SDL_Surface* image = SDL_LoadPNG(Utf8(fs::path(SDL_GetBasePath()) / "workspace" / "launcher" / "setup.png").c_str())) {
        art = SDL_CreateTextureFromSurface(renderer, image);
        if (art != nullptr) SDL_SetTextureScaleMode(art, SDL_SCALEMODE_LINEAR);
        if (SDL_Surface* soft = BlurredSurface(image)) {
            artBlur = SDL_CreateTextureFromSurface(renderer, soft);
            if (artBlur != nullptr) SDL_SetTextureScaleMode(artBlur, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(soft);
        }
        SDL_DestroySurface(image);
    }

    const fs::path base = fs::path(SDL_GetBasePath());
    const fs::path script = base / "workspace" / "LocalBuild.ps1";
    fs::path install = base / "MLB Power Pros 2008";
    auto gameExe = [&] { return install / "Game" / "MLBPowerPros2008.exe"; };
    // A language picked in the Setup goes to the installed game's Config.toml once it is installed.
    bool wroteLanguage = false;
    auto saveLanguage = [&] {
        if (g_lang == initialLang && !wroteLanguage) return;
        SaveLanguage(install / "Game" / "UserData" / "Config.toml");
        wroteLanguage = true;
    };
    // A disc image dropped onto the exe (or passed as the first argument) is preselected.
    std::string iso;
    if (int count = 0; LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count)) {
        if (count > 1) iso = Utf8(fs::path(args[1]));
        LocalFree(args);
    }
    bool shortcut = true;
    // MLBPP_SETUP_AUTOINSTALL=1 (scripted tests): start installing the preselected image right away,
    // without a desktop shortcut.
    const char* autoInstall = std::getenv("MLBPP_SETUP_AUTOINSTALL");
    const bool autoStart = autoInstall != nullptr && std::strcmp(autoInstall, "1") == 0 && !iso.empty();
    if (autoStart) shortcut = false;
    Build build;
    DialogResult isoDialog;
    DialogResult folderDialog;
    enum class Screen { Welcome, Building, Done, Failed } screen = Screen::Welcome;
    int menu = 0;
    const Uint64 start = SDL_GetTicks();
    Uint64 buildStart = 0;
    bool quit = false;
    if (autoStart) {
        build.Start(script, iso, install, shortcut);
        buildStart = SDL_GetTicks();
        screen = Screen::Building;
    }

    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) quit = true;
        }
        {
            std::lock_guard lock(isoDialog.mutex);
            if (isoDialog.ready) {
                isoDialog.ready = false;
                if (!isoDialog.path.empty()) iso = isoDialog.path, menu = 0;
            }
        }
        {
            std::lock_guard lock(folderDialog.mutex);
            if (folderDialog.ready) {
                folderDialog.ready = false;
                if (!folderDialog.path.empty()) install = fs::u8path(folderDialog.path) / "MLB Power Pros 2008";
            }
        }
        if (screen == Screen::Building && build.finished) {
            screen = build.succeeded ? Screen::Done : Screen::Failed;
            if (screen == Screen::Done) saveLanguage();
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ImVec2 size = ImGui::GetMainViewport()->Size;
        const float w = size.x;
        const float h = size.y;
        const float u = std::min(h, w * 0.75f) / 720.0f;
        const float t = static_cast<float>(SDL_GetTicks() - start) / 1000.0f;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        auto pressed = [&](std::initializer_list<ImGuiKey> keys) {
            if (!focused) return false;
            for (ImGuiKey key : keys) {
                if (ImGui::IsKeyPressed(key, true)) return true;
            }
            return false;
        };
        const bool left = pressed({ImGuiKey_LeftArrow, ImGuiKey_UpArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft});
        const bool right = pressed({ImGuiKey_RightArrow, ImGuiKey_DownArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight});
        const bool accept = pressed({ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_GamepadFaceDown});

        const bool installed = fs::exists(gameExe());
        const char* activated = nullptr;
        // Language switch with its top-right corner at `corner`. A change made after installing also
        // reaches the installed game's Config.toml.
        auto languageSwitch = [&](ImVec2 corner, float lu) {
            const float ph = 34 * lu;
            const float ts = ph * 0.56f;
            const float pw = (std::max(TextSize(g_bold, ts, "ES").x, TextSize(g_bold, ts, "EN").x) + ph * 0.8f) * 2.0f;
            const Lang picked = LanguagePill(dl, ImVec2(corner.x - pw, corner.y), ph, lu, click);
            if (picked != g_lang) {
                g_lang = picked;
                SDL_SetWindowTitle(window, windowTitle());
                if (screen == Screen::Done) saveLanguage();
            }
        };
        if (art != nullptr) {
            // MoonInning art (workspace\launcher\setup.png: the launcher backdrop with blank buttons).
            // Actions sit on the four art buttons by colour: orange = main action, blue = secondary,
            // green = install folder, purple = leave. Unused buttons are dimmed.
            constexpr float kW = 1672.0f;
            constexpr float kH = 941.0f;
            struct Slot {
                float x0, y0, x1, y1;
                ImU32 rim;
            };
            static constexpr Slot kSlots[4] = {{97, 335, 632, 421, IM_COL32(150, 50, 0, 255)},
                                               {98, 437, 626, 515, IM_COL32(20, 60, 160, 255)},
                                               {98, 535, 626, 612, IM_COL32(10, 90, 30, 255)},
                                               {98, 633, 626, 711, IM_COL32(90, 20, 120, 255)}};
            static constexpr float kSparks[3][4] = {{-27, -30, -12, -9}, {-60, -13, -31, 3}, {-67, 27, -36, 29}};
            struct Action {
                const char* label;
                int slot;
            };
            std::vector<Action> actions;
            switch (screen) {
            case Screen::Welcome:
                if (installed) actions.push_back({"JUGAR", 0});
                if (!installed) actions.push_back({iso.empty() ? "ELEGIR DISCO" : "INSTALAR", 0});
                if (installed) actions.push_back({"REINSTALAR", 1});
                else if (!iso.empty()) actions.push_back({"CAMBIAR DISCO", 1});
                actions.push_back({"CARPETA", 2});
                actions.push_back({"SALIR", 3});
                break;
            case Screen::Building: actions.push_back({"CANCELAR", 3}); break;
            case Screen::Done: actions = {{"JUGAR", 0}, {"SALIR", 3}}; break;
            case Screen::Failed: actions = {{"REINTENTAR", 0}, {"VER REGISTRO", 1}, {"SALIR", 3}}; break;
            }
            const int n = static_cast<int>(actions.size());
            menu = std::clamp(menu, 0, n - 1);
            if (left) menu = (menu + n - 1) % n;
            if (right) menu = (menu + 1) % n;

            const ImTextureID tex = reinterpret_cast<ImTextureID>(art);
            const float s = std::min(w / kW, h / kH);
            const ImVec2 o((w - kW * s) * 0.5f, (h - kH * s) * 0.5f);
            auto at = [&](float x, float y) { return ImVec2(o.x + x * s, o.y + y * s); };
            const ImVec2 m = ImGui::GetIO().MousePos;
            auto hit = [&](float x0, float y0, float x1, float y1) {
                const ImVec2 a = at(x0, y0);
                const ImVec2 b = at(x1, y1);
                return m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y;
            };
            // Other window shapes: the art blurred and darkened covers the window, with a soft shadow
            // around the art (same as the game's launcher).
            const float cover = std::max(w / kW, h / kH);
            const ImVec2 co((w - kW * cover) * 0.5f, (h - kH * cover) * 0.5f);
            dl->AddImage(artBlur != nullptr ? reinterpret_cast<ImTextureID>(artBlur) : tex, co,
                         ImVec2(co.x + kW * cover, co.y + kH * cover), ImVec2(0, 0), ImVec2(1, 1), Rgba(70, 80, 105));
            const float shadow = 28 * s;
            const ImU32 dark = Rgba(0, 0, 0, 150);
            const ImU32 clear = Rgba(0, 0, 0, 0);
            if (o.x > 1.0f) {
                dl->AddRectFilledMultiColor(ImVec2(o.x - shadow, 0), ImVec2(o.x, h), clear, dark, dark, clear);
                dl->AddRectFilledMultiColor(ImVec2(w - o.x, 0), ImVec2(w - o.x + shadow, h), dark, clear, clear, dark);
            }
            if (o.y > 1.0f) {
                dl->AddRectFilledMultiColor(ImVec2(0, o.y - shadow), ImVec2(w, o.y), clear, clear, dark, dark);
                dl->AddRectFilledMultiColor(ImVec2(0, h - o.y), ImVec2(w, h - o.y + shadow), dark, dark, clear, clear);
            }
            dl->AddImage(tex, at(0, 0), at(kW, kH));

            int selectedSlot = actions[static_cast<size_t>(menu)].slot;
            for (int slot = 0; slot < 4; ++slot) {
                const Slot& r = kSlots[slot];
                const Action* action = nullptr;
                int index = -1;
                for (int i = 0; i < n; ++i) {
                    if (actions[static_cast<size_t>(i)].slot == slot) action = &actions[static_cast<size_t>(i)], index = i;
                }
                const float radius = (r.y1 - r.y0) * 0.5f * s;
                if (action == nullptr) {
                    dl->AddRectFilled(at(r.x0, r.y0), at(r.x1, r.y1), Rgba(6, 24, 80, 175), radius);
                    continue;
                }
                const bool mouseOver = hit(r.x0, r.y0, r.x1, r.y1);
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                if (mouseOver && (delta.x != 0 || delta.y != 0)) menu = index, selectedSlot = slot;
                if (mouseOver && click) activated = action->label;
                float x0 = r.x0, y0 = r.y0, x1 = r.x1, y1 = r.y1;
                float bob = 0.0f;
                if (slot == selectedSlot) {
                    const float pulse = 0.5f + 0.5f * std::sin(t * 5.0f);
                    const float grow = 1.04f + 0.01f * pulse;
                    bob = std::sin(t * 4.0f) * 2.0f;
                    const float cxs = (r.x0 + r.x1) * 0.5f;
                    const float cys = (r.y0 + r.y1) * 0.5f + bob;
                    const float hw = (r.x1 - r.x0) * 0.5f * grow;
                    const float hh = (r.y1 - r.y0) * 0.5f * grow;
                    const float e = 8.0f + 5.0f * pulse;
                    dl->AddRectFilled(at(cxs - hw - e, cys - hh - e), at(cxs + hw + e, cys + hh + e),
                                      Rgba(255, 246, 150, static_cast<int>(110 + 80 * pulse)), (hh + e) * s);
                    x0 = cxs - hw, y0 = cys - hh, x1 = cxs + hw, y1 = cys + hh;
                    dl->AddImageRounded(tex, at(x0, y0), at(x1, y1), ImVec2(r.x0 / kW, r.y0 / kH), ImVec2(r.x1 / kW, r.y1 / kH),
                                        IM_COL32_WHITE, hh * s);
                    const float k = 1.0f + 0.12f * pulse;
                    for (const auto& seg : kSparks) {
                        const ImVec2 a = at(r.x0 + seg[0] * k, r.y0 + seg[1] * k + bob);
                        const ImVec2 b = at(r.x0 + seg[2] * k, r.y0 + seg[3] * k + bob);
                        dl->AddLine(a, b, IM_COL32_WHITE, 14.0f * s);
                        dl->AddLine(a, b, Rgba(255, 222, 40), 9.0f * s);
                    }
                } else {
                    dl->AddRectFilled(at(r.x0, r.y0), at(r.x1, r.y1), Rgba(6, 24, 80, 45), radius);
                }
                // Label in the art's lettering style: light fill, dark rim in the button's colour.
                const float room = (x1 - x0) - 210.0f;
                float fs = 44.0f * s;
                const char* label = Label(action->label);
                while (TextSize(g_title, fs, label).x > room * s) fs *= 0.94f;
                const float ty = at(0, (y0 + y1) * 0.5f).y - fs * 0.62f;
                LogoText(dl, g_title, fs, ImVec2(at(x0 + 70, 0).x, ty), IM_COL32_WHITE, Rgba(255, 244, 214), label,
                         r.rim, Rgba(255, 255, 255, 0));
            }
            if (accept) activated = actions[static_cast<size_t>(menu)].label;

            // Info box (erased in the art) and the right-hand log panel while building.
            const float ix = at(100, 0).x;
            const float rightEdge = at(1578, 0).x;
            auto rightText = [&](const std::string& text, float size, float y, ImU32 color) {
                const float tw = TextSize(g_text, size, text.c_str()).x;
                ShadowedText(dl, g_text, size, ImVec2(rightEdge - tw, at(0, y).y), color, text.c_str());
            };
            const ImU32 dim = Rgba(200, 225, 255);
            switch (screen) {
            case Screen::Welcome: {
                const std::string first = !iso.empty() ? Tr("Disco: ", "Disc: ") + Utf8(fs::u8path(iso).filename())
                                          : installed ? std::string(Tr("El juego ya está instalado. Pulsa JUGAR.",
                                                                       "The game is already installed. Press PLAY."))
                                                      : std::string(Tr("Elige tu copia de MLB Power Pros 2008 para Wii (USA).",
                                                                       "Choose your copy of MLB Power Pros 2008 for Wii (USA)."));
                ShadowedText(dl, g_text, 30 * s, at(100, 777), IM_COL32_WHITE, first.c_str());
                // Desktop shortcut checkbox, right side of the info box.
                const char* sc = Tr("Acceso directo en el escritorio", "Desktop shortcut");
                const float tw = TextSize(g_text, 22 * s, sc).x;
                const ImVec2 ca(rightEdge - tw - 34 * s, at(0, 818).y);
                // Long folder paths are shortened from the left so they never reach the checkbox.
                std::string folder = Utf8(install);
                const std::string folderLabel = Tr("Carpeta: ", "Folder: ");
                const float room = ca.x - ix - 30 * s - TextSize(g_text, 21 * s, (folderLabel + "…").c_str()).x;
                bool cut = false;
                while (!folder.empty() && TextSize(g_text, 21 * s, folder.c_str()).x > room) {
                    folder.erase(0, 1);
                    while (!folder.empty() && (static_cast<unsigned char>(folder[0]) & 0xC0) == 0x80) folder.erase(0, 1);
                    cut = true;
                }
                const std::string folderLine = folderLabel + (cut ? "…" : "") + folder;
                ShadowedText(dl, g_text, 21 * s, at(100, 820), dim, folderLine.c_str());
                const ImVec2 cb(ca.x + 24 * s, ca.y + 24 * s);
                dl->AddRectFilled(ca, cb, IM_COL32_WHITE, 5 * s);
                if (shortcut) DrawCheck(dl, ImVec2(ca.x + 4 * s, ca.y + 3 * s), 17 * s, Rgba(30, 140, 60));
                ShadowedText(dl, g_text, 22 * s, ImVec2(cb.x + 10 * s, ca.y), IM_COL32_WHITE, sc);
                if (click && m.x >= ca.x && m.x < rightEdge && m.y >= ca.y && m.y < cb.y) shortcut = !shortcut;
                rightText(Tr("Se aceptan ISO, NKit, RVZ, WBFS, WIA, GCZ y CISO.", "Supports ISO, NKit, RVZ, WBFS, WIA, GCZ and CISO."),
                          21 * s, 782, dim);
                break;
            }
            case Screen::Building: {
                std::lock_guard lock(build.mutex);
                ShadowedText(dl, g_text, 28 * s, at(100, 777), IM_COL32_WHITE, build.message.c_str());
                const Uint64 secs = (SDL_GetTicks() - buildStart) / 1000;
                const std::string status = std::to_string(build.percent) + "%   ·   " + std::to_string(secs / 60) + ":" +
                                           (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60);
                rightText(status, 24 * s, 780, dim);
                const ImVec2 pa = at(100, 818);
                const ImVec2 pb = at(1578, 842);
                dl->AddRectFilled(pa, pb, Rgba(8, 36, 110, 235), 12 * s);
                const float fill = pa.x + (pb.x - pa.x) * static_cast<float>(std::clamp(build.percent, 0, 100)) / 100.0f;
                if (fill > pa.x + 24 * s) Pill(dl, pa, ImVec2(fill, pb.y), Rgba(255, 210, 100), Rgba(242, 118, 18), s * 0.5f);
                if (!build.tail.empty()) {
                    const ImVec2 la = at(700, 430);
                    const ImVec2 lb = at(1600, 600);
                    dl->AddRectFilled(la, lb, Rgba(6, 26, 90, 190), 16 * s);
                    dl->AddRect(la, lb, Rgba(140, 200, 255, 160), 16 * s, 0, 2 * s);
                    // One line per log entry, clipped to the panel.
                    dl->PushClipRect(ImVec2(la.x + 14 * s, la.y), ImVec2(lb.x - 14 * s, lb.y), true);
                    float ly = la.y + 16 * s;
                    for (const std::string& l : build.tail) {
                        Text(dl, g_text, 20 * s, ImVec2(la.x + 18 * s, ly), Rgba(190, 220, 250), l.c_str());
                        ly += 36 * s;
                    }
                    dl->PopClipRect();
                }
                break;
            }
            case Screen::Done:
                ShadowedText(dl, g_text, 30 * s, at(100, 777), IM_COL32_WHITE,
                             Tr("¡Listo! MLB Power Pros 2008 está instalado.", "Done! MLB Power Pros 2008 is installed."));
                ShadowedText(dl, g_text, 21 * s, at(100, 820), dim, Utf8(gameExe()).c_str());
                if (shortcut) rightText(Tr("También hay un acceso directo en el escritorio.", "A desktop shortcut was created too."), 21 * s, 782, dim);
                break;
            case Screen::Failed: {
                std::lock_guard lock(build.mutex);
                ShadowedText(dl, g_text, 28 * s, at(100, 777), Rgba(255, 200, 180), build.error.c_str());
                const std::string logLine = Tr("Registro: ", "Log: ") + Utf8(install / "setup.log");
                ShadowedText(dl, g_text, 21 * s, at(100, 820), dim, logLine.c_str());
                break;
            }
            }
            rightText(Tr("No incluye nada del juego", "No game files included"), 22 * s, 888, dim);
            if (g_lang == Lang::En) {
                // The art's button hints are lettered in Spanish: cover each word with the backdrop
                // just above and below it (the art's own gradient) and letter it in English.
                struct Word {
                    float x0, x1;
                    const char* text;
                };
                static constexpr Word kHintWords[3] = {{114, 206, "Accept"}, {283, 358, "Back"}, {435, 510, "Move"}};
                for (const Word& word : kHintWords) {
                    dl->AddImage(tex, at(word.x0, 884), at(word.x1, 900), ImVec2(word.x0 / kW, 868 / kH), ImVec2(word.x1 / kW, 884 / kH));
                    dl->AddImage(tex, at(word.x0, 900), at(word.x1, 918), ImVec2(word.x0 / kW, 918 / kH), ImVec2(word.x1 / kW, 936 / kH));
                    ShadowedText(dl, g_text, 30 * s, at(word.x0 + 6, 883), IM_COL32_WHITE, word.text);
                }
            }
            languageSwitch(at(1630, 72), 1.3f * s);
        } else {
            DrawBackground(dl, w, h, u, t, g_title);
            // Title.
            const float cx = w * 0.5f;
            LogoTextCentered(dl, g_title, 34 * u, cx - 150 * u, h * 0.09f, Rgba(110, 170, 255), Rgba(24, 70, 180), "MLB");
            const ImVec2 ts = LogoTextCentered(dl, g_title, 84 * u, cx, h * 0.09f + 30 * u, Rgba(255, 110, 96),
                                               Rgba(196, 18, 30), "POWER PROS");
            LogoText(dl, g_title, 46 * u, ImVec2(cx + ts.x * 0.5f - 130 * u, h * 0.09f + 114 * u), Rgba(255, 96, 70),
                     Rgba(200, 16, 20), "2008", Rgba(255, 224, 70));

            // Pills for the current screen.
            struct Item {
                const char* label;
                ImU32 top, bottom, rim;
            };
            const Item orange{"", Rgba(255, 206, 96), Rgba(240, 112, 18), Rgba(150, 50, 0)};
            const Item green{"", Rgba(160, 244, 120), Rgba(34, 164, 58), Rgba(10, 90, 30)};
            const Item purple{"", Rgba(244, 160, 244), Rgba(168, 64, 200), Rgba(90, 20, 120)};
            std::vector<std::pair<const char*, Item>> items;
            switch (screen) {
            case Screen::Welcome:
                if (installed) items.push_back({"JUGAR", orange});
                items.push_back({iso.empty() ? "ELEGIR DISCO" : (installed ? "REINSTALAR" : "INSTALAR"), installed ? green : orange});
                if (!iso.empty() || installed) items.push_back({iso.empty() ? "OTRO DISCO" : "CAMBIAR DISCO", installed ? purple : green});
                items.push_back({"SALIR", purple});
                break;
            case Screen::Building: items.push_back({"CANCELAR", purple}); break;
            case Screen::Done: items.push_back({"JUGAR", orange}); items.push_back({"SALIR", purple}); break;
            case Screen::Failed: items.push_back({"REINTENTAR", orange}); items.push_back({"VER REGISTRO", green}); items.push_back({"SALIR", purple}); break;
            }
            const int n = static_cast<int>(items.size());
            menu = std::clamp(menu, 0, n - 1);
            if (left) menu = (menu + n - 1) % n;
            if (right) menu = (menu + 1) % n;
            const float pillW = std::min(250 * u, (w * 0.9f - 30 * u * static_cast<float>(n - 1)) / static_cast<float>(n));
            const float pillH = 78 * u;
            const float gap = 30 * u;
            const float rowX = cx - (pillW * static_cast<float>(n) + gap * static_cast<float>(n - 1)) * 0.5f;
            const float rowY = h * 0.42f;
            for (int i = 0; i < n; ++i) {
                const bool selected = menu == i;
                const float grow = selected ? 1.08f : 1.0f;
                const ImVec2 c(rowX + pillW * 0.5f + static_cast<float>(i) * (pillW + gap),
                               rowY + pillH * 0.5f + (selected ? std::sin(t * 4.0f) * 3.0f * u : 0.0f));
                const ImVec2 a(c.x - pillW * 0.5f * grow, c.y - pillH * 0.5f * grow);
                const ImVec2 b(c.x + pillW * 0.5f * grow, c.y + pillH * 0.5f * grow);
                const ImVec2 m = ImGui::GetIO().MousePos;
                const bool hover = m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y;
                if (hover && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0)) menu = i;
                if (hover && click) activated = items[static_cast<size_t>(i)].first;
                if (selected) {
                    const float pulse = 0.5f + 0.5f * std::sin(t * 5.0f);
                    const float e = (10 + 4 * pulse) * u;
                    dl->AddRectFilled(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e),
                                      Rgba(255, 244, 140, static_cast<int>(140 + 70 * pulse)), (b.y - a.y) * 0.5f + e);
                }
                const Item& item = items[static_cast<size_t>(i)].second;
                Pill(dl, a, b, item.top, item.bottom, u, selected ? Rgba(255, 250, 200) : IM_COL32_WHITE);
                const char* label = Label(items[static_cast<size_t>(i)].first);
                float fs = 32 * u * grow;
                while (TextSize(g_title, fs, label).x > (b.x - a.x) * 0.84f) fs *= 0.92f;
                LogoTextCentered(dl, g_title, fs, c.x, c.y - fs * 0.62f, IM_COL32_WHITE, Rgba(255, 240, 190), label,
                                 item.rim, Rgba(255, 255, 255, 0));
            }
            if (accept) activated = items[static_cast<size_t>(menu)].first;

            // Info box.
            const ImVec2 ia(w * 0.06f, h * 0.60f);
            const ImVec2 ib(w * 0.94f, h * 0.88f);
            dl->AddRectFilled(ImVec2(ia.x, ia.y + 6 * u), ImVec2(ib.x, ib.y + 6 * u), Rgba(0, 20, 70, 110), 16 * u);
            dl->AddRectFilled(ia, ib, IM_COL32_WHITE, 16 * u);
            const int infoStart = dl->VtxBuffer.Size;
            dl->AddRectFilled(ImVec2(ia.x + 4 * u, ia.y + 4 * u), ImVec2(ib.x - 4 * u, ib.y - 4 * u), IM_COL32_WHITE, 13 * u);
            Shade(dl, infoStart, ia.y, ib.y, Rgba(40, 120, 220), Rgba(14, 58, 150));
            const float ix = ia.x + 32 * u;
            const float wrap = ib.x - 32 * u - ix;
            float iy = ia.y + 22 * u;
            auto line = [&](const std::string& text, float size, ImU32 color) {
                ShadowedText(dl, g_text, size, ImVec2(ix, iy), color, text.c_str(), wrap);
                iy += g_text->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str()).y + 8 * u;
            };
            const ImU32 dim = Rgba(190, 225, 255);
            switch (screen) {
            case Screen::Welcome:
                if (iso.empty()) {
                    line(installed ? Tr("El juego ya está instalado. Pulsa JUGAR.", "The game is already installed. Press PLAY.")
                                   : Tr("Elige tu copia de MLB Power Pros 2008 para Wii (USA).",
                                        "Choose your copy of MLB Power Pros 2008 for Wii (USA)."),
                         26 * u, IM_COL32_WHITE);
                    line(Tr("Se aceptan ISO, NKit, RVZ, WBFS, WIA, GCZ y CISO. El juego se traduce y compila en tu PC; "
                            "no se descarga ni se envía nada del juego.",
                            "Supports ISO, NKit, RVZ, WBFS, WIA, GCZ and CISO. The game is translated and compiled on "
                            "your PC; nothing from the game is downloaded or sent."),
                         21 * u, dim);
                } else {
                    line(Tr("Disco: ", "Disc: ") + Utf8(fs::u8path(iso).filename()), 25 * u, IM_COL32_WHITE);
                    line(Tr("La primera instalación tarda varios minutos y usa todos los núcleos del procesador.",
                            "The first install takes several minutes and uses every CPU core."),
                         21 * u, dim);
                }
                line(Tr("Carpeta: ", "Folder: ") + Utf8(install), 20 * u, dim);
                break;
            case Screen::Building: {
                std::lock_guard lock(build.mutex);
                line(build.message, 26 * u, IM_COL32_WHITE);
                const ImVec2 pa(ix, iy + 4 * u);
                const ImVec2 pb(ib.x - 32 * u, iy + 34 * u);
                dl->AddRectFilled(pa, pb, Rgba(8, 40, 120, 230), 15 * u);
                const float fill = pa.x + (pb.x - pa.x) * static_cast<float>(std::clamp(build.percent, 0, 100)) / 100.0f;
                if (fill > pa.x + 30 * u) Pill(dl, pa, ImVec2(fill, pb.y), Rgba(255, 210, 100), Rgba(240, 120, 20), u * 0.5f);
                const std::string pct = std::to_string(build.percent) + "%";
                const Uint64 secs = (SDL_GetTicks() - buildStart) / 1000;
                const std::string elapsed = pct + "   ·   " + std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60);
                iy = pb.y + 10 * u;
                line(elapsed, 20 * u, dim);
                for (const std::string& l : build.tail) {
                    if (iy + 18 * u > ib.y - 10 * u) break;  // keep the log inside the box
                    Text(dl, g_text, 15 * u, ImVec2(ix, iy), Rgba(170, 205, 240), l.c_str());
                    iy += 18 * u;
                }
                break;
            }
            case Screen::Done:
                line(Tr("¡Listo! MLB Power Pros 2008 está instalado.", "Done! MLB Power Pros 2008 is installed."), 28 * u, IM_COL32_WHITE);
                line(Tr("Ejecutable: ", "Executable: ") + Utf8(gameExe()), 20 * u, dim);
                if (shortcut) line(Tr("También hay un acceso directo en el escritorio.", "A desktop shortcut was created too."), 20 * u, dim);
                break;
            case Screen::Failed: {
                std::lock_guard lock(build.mutex);
                line(Tr("No se pudo instalar.", "Installation failed."), 28 * u, Rgba(255, 190, 170));
                line(build.error, 22 * u, IM_COL32_WHITE);
                line(Tr("Registro: ", "Log: ") + Utf8(install / "setup.log"), 18 * u, dim);
                break;
            }
            }
            if (screen == Screen::Welcome) {
                // Options: install folder and desktop shortcut.
                const float oy = ib.y - 46 * u;
                const char* folder = Tr("Cambiar carpeta", "Change folder");
                const ImVec2 fa(ix, oy);
                const ImVec2 fb(ix + TextSize(g_bold, 19 * u, folder).x + 36 * u, oy + 34 * u);
                const ImVec2 m = ImGui::GetIO().MousePos;
                Pill(dl, fa, fb, Rgba(90, 150, 240), Rgba(30, 80, 190), u * 0.5f);
                Text(dl, g_bold, 19 * u, ImVec2(fa.x + 18 * u, fa.y + 7 * u), IM_COL32_WHITE, folder);
                if (click && m.x >= fa.x && m.x < fb.x && m.y >= fa.y && m.y < fb.y) {
                    SDL_ShowOpenFolderDialog(OnDialog, &folderDialog, window, nullptr, false);
                }
                const ImVec2 ca(fb.x + 30 * u, oy + 5 * u);
                const ImVec2 cb(ca.x + 24 * u, ca.y + 24 * u);
                dl->AddRectFilled(ca, cb, IM_COL32_WHITE, 5 * u);
                if (shortcut) DrawCheck(dl, ImVec2(ca.x + 4 * u, ca.y + 3 * u), 17 * u, Rgba(30, 140, 60));
                const char* sc = Tr("Crear acceso directo en el escritorio", "Create a desktop shortcut");
                ShadowedText(dl, g_text, 20 * u, ImVec2(cb.x + 10 * u, oy + 5 * u), IM_COL32_WHITE, sc);
                const float scEnd = cb.x + 10 * u + TextSize(g_text, 20 * u, sc).x;
                if (click && m.x >= ca.x && m.x < scEnd && m.y >= ca.y && m.y < cb.y) shortcut = !shortcut;
            }

            float hx = w * 0.06f;
            const float hy = h - 50 * u;
            hx = Hint(dl, g_bold, g_text, hx, hy, u, Rgba(40, 170, 80), "A", Tr("Aceptar", "Accept"));
            Hint(dl, g_bold, g_text, hx, hy, u, Rgba(30, 90, 200), "+", Tr("Mover", "Move"));
            const char* noGame = Tr("No incluye nada del juego", "No game files included");
            Text(dl, g_text, 17 * u, ImVec2(w * 0.94f - TextSize(g_text, 17 * u, noGame).x, hy + 4 * u), Rgba(190, 225, 255), noGame);
            languageSwitch(ImVec2(w - 24 * u, 60 * u), u);
        }

        // Actions.
        if (activated != nullptr) {
            const std::string a = activated;
            static const SDL_DialogFileFilter filtersEs[] = {
                {"Imagen de disco de Wii", "iso;rvz;wbfs;wia;gcz;ciso;gcm;nfs"}, {"Todos los archivos", "*"}};
            static const SDL_DialogFileFilter filtersEn[] = {
                {"Wii disc image", "iso;rvz;wbfs;wia;gcz;ciso;gcm;nfs"}, {"All files", "*"}};
            const SDL_DialogFileFilter* filters = g_lang == Lang::En ? filtersEn : filtersEs;
            if (a == "CARPETA") {
                SDL_ShowOpenFolderDialog(OnDialog, &folderDialog, window, nullptr, false);
            } else if (a == "ELEGIR DISCO" || a == "CAMBIAR DISCO" || a == "OTRO DISCO") {
                SDL_ShowOpenFileDialog(OnDialog, &isoDialog, window, filters, 2, nullptr, false);
            } else if (a == "INSTALAR" || a == "REINSTALAR" || a == "REINTENTAR") {
                if (iso.empty()) {
                    SDL_ShowOpenFileDialog(OnDialog, &isoDialog, window, filters, 2, nullptr, false);
                } else {
                    build.Start(script, iso, install, shortcut);
                    buildStart = SDL_GetTicks();
                    screen = Screen::Building;
                    menu = 0;
                }
            } else if (a == "CANCELAR") {
                build.Stop();
                screen = Screen::Welcome;
            } else if (a == "VER REGISTRO") {
                ShellExecuteW(nullptr, L"open", (install / "setup.log").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (a == "JUGAR") {
                const fs::path exe = gameExe();
                ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, exe.parent_path().c_str(), SW_SHOWNORMAL);
                quit = true;
            } else if (a == "SALIR") {
                quit = true;
            }
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 8, 56, 158, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    build.Stop();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
