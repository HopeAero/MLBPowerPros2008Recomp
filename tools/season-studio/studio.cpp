// SPDX-License-Identifier: GPL-3.0-or-later
// Power Pros Wii 2008 Season Studio: what a new Season mode game or MLB Life career will use, each mod's
// season settings (start_year, roster_year, dh, leagues / [divisions] in its mod.toml), the major leagues'
// real schedules (statsapi.mlb.com, saved as schedule/<year>.csv) and a month view of a year's games.
// It writes only mod.toml keys, schedule/*.csv and Config.toml [mods] enabled; it reads no game data.
#include "config.h"
#include "ui.h"

#include "season_pack.h"
#include "season_schedule.h"
#include "season_schedule_mlb.h"
#include "season_text.h"
#include "toml.hpp"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include <atomic>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <thread>

namespace {
namespace fs = std::filesystem;
namespace ui = tstudio::ui;
using power_pros_ui::Tr;
using tstudio::PathFromUtf8;
using tstudio::Utf8;

// ---- Mods: what season_pack::Make reads, from each mod.toml (the runtime's rules, src/mods.cpp) ----
struct Mod {
    std::string id, name;
    fs::path root;
    bool enabled = false;
    int startYear = 0, rosterYear = 0;
    bool universalDh = false, originalDh = false;
    std::vector<std::string> divisions[6];
    bool hasDivisions = false, originalLeagues = false;
    std::vector<int> years;  // schedule/<year>.csv, sorted
};

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<Mod> Discover(const fs::path& modsDir, const std::vector<std::string>& enabled) {
    std::vector<Mod> found;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(modsDir, ec)) {
        if (!entry.is_directory(ec)) continue;
        Mod mod;
        mod.root = entry.path();
        mod.id = mod.name = Utf8(entry.path().filename());
        const fs::path manifest = mod.root / "mod.toml";
        if (fs::is_regular_file(manifest, ec)) {
            try {
                std::ifstream input(manifest, std::ios::binary);
                const toml::value doc = toml::parse(input, Utf8(manifest));
                mod.name = toml::find_or<std::string>(doc, "name", mod.id);
                const int64_t start = toml::find_or<int64_t>(doc, "start_year", 0);
                if (start >= 2008 && start <= 2099) mod.startYear = static_cast<int>(start);
                const int64_t roster = toml::find_or<int64_t>(doc, "roster_year", 0);
                if (roster >= 2008 && roster <= 2099) mod.rosterYear = static_cast<int>(roster);
                const std::string dh = Lower(toml::find_or<std::string>(doc, "dh", ""));
                mod.universalDh = dh == "both";
                mod.originalDh = dh == "original";
                mod.originalLeagues = Lower(toml::find_or<std::string>(doc, "leagues", "")) == "original";
                if (doc.contains("divisions") && doc.at("divisions").is_table()) {
                    const toml::value& table = doc.at("divisions");
                    for (int d = 0; d < 6; ++d) {
                        mod.divisions[d] = toml::find_or<std::vector<std::string>>(table, season_schedule::kDivisionKeys[d], {});
                        mod.hasDivisions = mod.hasDivisions || !mod.divisions[d].empty();
                    }
                }
            } catch (const std::exception&) {  // unreadable: a mod with its folder name only, as the game
            }
        }
        mod.years = season_pack::ScheduleYears(mod.root);
        std::sort(mod.years.begin(), mod.years.end());
        mod.enabled = std::find(enabled.begin(), enabled.end(), mod.id) != enabled.end();
        found.push_back(std::move(mod));
    }
    std::sort(found.begin(), found.end(), [](const Mod& a, const Mod& b) { return Lower(a.name) < Lower(b.name); });
    return found;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::string(std::istreambuf_iterator<char>(in), {}) : std::string();
}

// ---- Teams and dates ---------------------------------------------------------------------------
// By team number (season_schedule.h kTeamCodes); names as of `year` where they changed.
const char* TeamCode(int team, int year) {
    static const char* kCodes[30] = {"BOS", "NYY", "TOR", "BAL", "TB",  "CLE", "DET", "MIN", "CWS", "KC",
                                     "LAA", "SEA", "OAK", "TEX", "PHI", "NYM", "ATL", "WSH", "FLA", "CHC",
                                     "MIL", "STL", "HOU", "CIN", "PIT", "ARI", "COL", "SD",  "LAD", "SF"};
    if (team == 18 && year >= 2012) return "MIA";
    return team >= 0 && team < 30 ? kCodes[team] : "?";
}
const char* TeamName(int team, int year) {
    static const char* kNames[30] = {"Red Sox",   "Yankees",  "Blue Jays", "Orioles",  "Rays",     "Indians",
                                     "Tigers",    "Twins",    "White Sox", "Royals",   "Angels",   "Mariners",
                                     "Athletics", "Rangers",  "Phillies",  "Mets",     "Braves",   "Nationals",
                                     "Marlins",   "Cubs",     "Brewers",   "Cardinals", "Astros",  "Reds",
                                     "Pirates",   "Diamondbacks", "Rockies", "Padres", "Dodgers",  "Giants"};
    if (team == 5 && year >= 2022) return "Guardians";
    return team >= 0 && team < 30 ? kNames[team] : "?";
}

// Slot day (0 = March 1) to month and day.
std::pair<int, int> MonthDay(int index) {
    static constexpr int kMonthFirst[13] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366};
    const int doy = index + season_schedule::kFirstDay;
    int m = 1;
    while (m < 12 && doy >= kMonthFirst[m]) ++m;
    return {m, doy - kMonthFirst[m - 1] + 1};
}

std::string Clock(int minutes) {
    char text[8];
    std::snprintf(text, sizeof text, "%d:%02d", minutes / 60, minutes % 60);
    return text;
}

// ---- Paths, language, the enhancements ------------------------------------------------------------
struct Paths {
    fs::path config, mods, game;
};

std::vector<std::string> WideArgs(int argc, char** argv) {
    std::vector<std::string> out;
#if defined(_WIN32)
    (void)argc, (void)argv;
    int count = 0;
    if (LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) out.push_back(Utf8(fs::path(args[i])));
        LocalFree(args);
    }
#else
    for (int i = 1; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

Paths ResolvePaths(const std::vector<std::string>& args) {
    const fs::path base = PathFromUtf8(SDL_GetBasePath());
    Paths p{base / "UserData" / "Config.toml", base / "mods", base / "MLBPowerPros2008.exe"};
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--config") p.config = PathFromUtf8(args[++i]);
        else if (args[i] == "--mods") p.mods = PathFromUtf8(args[++i]);
        else if (args[i] == "--game") p.game = PathFromUtf8(args[++i]);
    }
    return p;
}

bool ConfigFlag(const Paths& p, const char* key) {
    const auto v = fs::exists(p.config) ? tstudio::ReadSetting(p.config, "season", key) : std::nullopt;
    return v && *v == "true";
}
int MaxSeasons(const Paths& p) {
    const auto v = fs::exists(p.config) ? tstudio::ReadSetting(p.config, "season", "max_seasons") : std::nullopt;
    return v ? std::atoi(v->c_str()) : 10;
}

void OpenFolder(const fs::path& target) {
#if defined(_WIN32)
    ShellExecuteW(nullptr, L"open", target.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    const std::string path = target.string();
    char* argv[] = {const_cast<char*>("xdg-open"), const_cast<char*>(path.c_str()), nullptr};
    pid_t pid = 0;
    posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ);
#endif
}

// ---- Downloads ---------------------------------------------------------------------------------
// One year with the system's curl (Windows 10 and later, Linux) into <mod>/schedule/<year>.csv.
// Returns a line for the year, or sets `error`.
std::string FetchSchedule(int year, const fs::path& modDir, std::string& error) {
    std::error_code ec;
    fs::create_directories(modDir / "schedule", ec);
    const fs::path temp = modDir / "schedule" / ".download.json";
    fs::remove(temp, ec);
    const std::string url = season_schedule_mlb::Url(year);
#if defined(_WIN32)
    std::wstring command = L"curl.exe -sSfL --max-time 60 -o \"" + temp.wstring() + L"\" \"" +
                           std::wstring(url.begin(), url.end()) + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                       &process)) {
        WaitForSingleObject(process.hProcess, 90000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#else
    // No shell: the mod's folder name goes to curl as one argument, whatever it contains.
    std::string out = temp.string(), link = url;
    char* argv[] = {const_cast<char*>("curl"), const_cast<char*>("-sSfL"), const_cast<char*>("--max-time"), const_cast<char*>("60"),
                    const_cast<char*>("-o"), out.data(), const_cast<char*>("--"), link.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, "curl", nullptr, nullptr, argv, environ) == 0) waitpid(pid, nullptr, 0);
#endif
    const std::string json = ReadFile(temp);
    fs::remove(temp, ec);
    if (json.empty()) {
        error = Tr("no se pudo descargar. Comprueba la conexión a internet.", "could not be downloaded. Check the internet connection.");
        return {};
    }
    const season_schedule_mlb::Converted converted = season_schedule_mlb::ToCsv(json, year);
    if (!converted.error.empty()) {
        error = std::string(Tr("calendario no válido: ", "not a valid schedule: ")) + converted.error;
        return {};
    }
    std::ofstream(modDir / "schedule" / (std::to_string(year) + ".csv"), std::ios::binary) << converted.csv;
    const auto fitted = season_schedule::Fit(season_schedule::Parse(converted.csv).games);
    std::string line = std::to_string(year) + ": " + std::to_string(converted.games) + Tr(" partidos", " games");
    if (fitted.shift)
        line += std::string(Tr(", se juega ", ", played ")) + std::to_string(std::abs(fitted.shift)) +
                (std::abs(fitted.shift) == 1 ? Tr(" día", " day") : Tr(" días", " days")) +
                (fitted.shift < 0 ? Tr(" antes", " earlier") : Tr(" después", " later"));
    if (fitted.moved) line += ", " + std::to_string(fitted.moved) + Tr(" a un día cercano", " moved to a nearby day");
    return line;
}

struct Download {
    std::thread thread;
    std::mutex mutex;
    std::string status;  // under mutex
    std::vector<std::string> lines;
    std::string error;
    int first = 0;
    std::atomic<bool> running{false}, done{false};
};

// The offered years in from..to into modDir, one after another; stops at the first failure.
void RunDownload(Download& d, int from, int to, fs::path modDir) {
    if (from > to) std::swap(from, to);
    const auto offered = season_schedule_mlb::OfferedYears();
    for (int year = from; year <= to; ++year) {
        if (std::find(offered.begin(), offered.end(), year) == offered.end()) continue;
        {
            std::lock_guard lock(d.mutex);
            d.status = std::string(Tr("Descargando ", "Downloading ")) + std::to_string(year) + "...";
        }
        std::string error;
        const std::string line = FetchSchedule(year, modDir, error);
        std::lock_guard lock(d.mutex);
        if (!error.empty()) {
            d.error = std::to_string(year) + ": " + error;
            break;
        }
        d.lines.push_back(line);
        if (!d.first) d.first = year;
    }
    d.done = true;
}

// ---- The app -------------------------------------------------------------------------------------
struct App {
    Paths paths;
    std::vector<Mod> mods;
    int active = -1;
    season_pack::Plan plan;
    season_text::SeasonText text;
    int tab = 0;  // 0 settings, 1 calendar
    int fromYear = 2026, toYear = 2026;
    Download download;
    std::string downloadResult;
    // Calendar.
    int calYear = 0;
    int team = -1;  // -1: every team
    int month = 4;
    int pickedDay = -1;
    std::vector<season_schedule::Game> games;
    std::string calNote;
    // Messages.
    std::string toast;
    bool toastError = false;
    char newName[64] = {};
    std::string newError;
    bool openNew = false;
};

void Toast(App& a, std::string text, bool error = false) {
    a.toast = std::move(text);
    a.toastError = error;
}

Mod* Active(App& a) { return a.active >= 0 && a.active < static_cast<int>(a.mods.size()) ? &a.mods[a.active] : nullptr; }

void LoadCalendar(App& a) {
    a.games.clear();
    a.calNote.clear();
    a.pickedDay = -1;
    const Mod* mod = Active(a);
    if (!mod || !a.calYear) return;
    const auto parsed = season_schedule::Parse(ReadFile(mod->root / "schedule" / (std::to_string(a.calYear) + ".csv")));
    const auto fitted = season_schedule::Fit(parsed.games);
    a.games = fitted.games;
    a.calNote = std::to_string(a.games.size()) + Tr(" partidos, en las fechas en que los juega el juego",
                                                   " games, on the dates the game plays them");
    if (fitted.shift)
        a.calNote += std::string(Tr(" (", " (")) + std::to_string(std::abs(fitted.shift)) +
                     (std::abs(fitted.shift) == 1 ? Tr(" día ", " day ") : Tr(" días ", " days ")) + (fitted.shift < 0 ? Tr("antes que en la realidad", "earlier than for real") : Tr("después que en la realidad", "later than for real")) + ")";
    if (fitted.moved) a.calNote += ", " + std::to_string(fitted.moved) + Tr(" movidos a un día cercano", " moved to a nearby day");
    a.calNote += ".";
}

// Mods, the summary and the calendar again from the disk; keeps the active mod by id.
void Refresh(App& a, std::string keep = {}) {
    if (keep.empty() && Active(a)) keep = Active(a)->id;
    a.mods = Discover(a.paths.mods, fs::exists(a.paths.config) ? tstudio::ReadEnabledMods(a.paths.config) : std::vector<std::string>{});
    a.active = a.mods.empty() ? -1 : 0;
    for (size_t i = 0; i < a.mods.size(); ++i)
        if (a.mods[i].id == keep) a.active = static_cast<int>(i);
    a.plan = season_pack::Make(a.mods, ConfigFlag(a.paths, "universal_dh"), ConfigFlag(a.paths, "modern_leagues"),
                               [](const Mod& m) { return m.years; });
    season_pack::DropSameSchedules(a.plan, [&](const std::string& name) {
        for (const auto& m : a.mods)
            if (m.name == name) return m.root;
        return fs::path();
    });
    a.text = season_text::DescribeSeason(a.plan, MaxSeasons(a.paths));
    const Mod* mod = Active(a);
    if (mod && std::find(mod->years.begin(), mod->years.end(), a.calYear) == mod->years.end())
        a.calYear = mod->years.empty() ? 0 : mod->years.front();
    LoadCalendar(a);
}

// Writes the active mod's mod.toml through `edit` (season_pack.h keeps the rest of the file).
void EditManifest(App& a, const std::function<std::string(std::string)>& edit) {
    Mod* mod = Active(a);
    if (!mod) return;
    const std::string text = edit(ReadFile(mod->root / "mod.toml"));
    std::ofstream out(mod->root / "mod.toml", std::ios::binary);
    out << text;
    if (!out) Toast(a, Tr("No se pudo escribir mod.toml.", "Could not write mod.toml."), true);
    else Toast(a, Tr("Guardado. Vale para la próxima partida nueva (si el juego está abierto, ciérralo y ábrelo).",
                     "Saved. It applies to the next new game (if the game is open, close it and open it again)."));
    Refresh(a);
}

// A new mod folder with a mod.toml, switched on in the game.
bool CreateMod(App& a, std::string name) {
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
    if (name.empty() || name.find_first_of("\\/:*?\"<>|") != std::string::npos || name == "." || name == "..") {
        a.newError = Tr("Pon un nombre sin estos signos: \\ / : * ? \" < > |", "Use a name without these signs: \\ / : * ? \" < > |");
        return false;
    }
    const fs::path dir = a.paths.mods / PathFromUtf8(name);
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        a.newError = Tr("Ya hay un mod con ese nombre.", "There is already a mod with that name.");
        return false;
    }
    fs::create_directories(dir, ec);
    std::ofstream(dir / "mod.toml", std::ios::binary)
        << "name = \"" << name << "\"\ndescription = \""
        << Tr("Temporada hecha con el Editor de temporada.", "Season made with the Season Editor.") << "\"\n";
    if (fs::exists(a.paths.config)) tstudio::SetModEnabled(a.paths.config, name, true);
    Refresh(a, name);
    Toast(a, Tr("Mod creado y activado.", "Mod created and switched on."));
    return true;
}

// ---- Drawing -------------------------------------------------------------------------------------
// A choice list; returns the new index when changed, else -1.
int Choice(const char* id, const std::vector<std::string>& items, int current, float width, bool enabled = true) {
    ImGui::SetNextItemWidth(width);
    ImGui::BeginDisabled(!enabled);
    int picked = -1;
    if (ImGui::BeginCombo(id, current >= 0 && current < static_cast<int>(items.size()) ? items[current].c_str() : "")) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (ImGui::Selectable(items[i].c_str(), i == current) && i != current) picked = i;
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    return picked;
}

void Muted(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::kMuted));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void Heading(const char* text) {
    ImGui::PushFont(power_pros_ui::g_menuBold);
    ImGui::SetWindowFontScale(0.8f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kYellow), "%s", text);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
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
    Text(dl, g_bold, 15, ImVec2(20, 41), ui::kYellow, Tr("E D I T O R   D E   T E M P O R A D A", "S E A S O N   E D I T O R"));
    ImGui::SetCursorScreenPos(ImVec2(w - 100, 14));
    Lang lang = g_lang;
    if (ui::LangSwitch(&lang)) {
        g_lang = lang;
        if (fs::exists(a.paths.config)) tstudio::WriteSetting(a.paths.config, "ui", "language", lang == Lang::En ? "\"en\"" : "\"es\"");
        Refresh(a);
    }
}

// Left: what a new game will use, with every mod listed (switched on or not).
void DrawSummary(App& a, ImVec2 size) {
    ui::BeginPanel("##summary", Tr("PRÓXIMA PARTIDA NUEVA", "NEXT NEW GAME"), size);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(a.text.help.c_str());
    ImGui::PopTextWrapPos();
    ui::EndPanel();
}

void DrawModBar(App& a) {
    Heading(Tr("Mod", "Mod"));
    ImGui::SameLine(80);
    std::vector<std::string> names;
    for (const auto& m : a.mods) names.push_back(m.name + (m.enabled ? "" : Tr("  (desactivado)", "  (switched off)")));
    if (names.empty()) names.push_back(Tr("(no hay mods)", "(no mods)"));
    if (const int picked = Choice("##mod", names, std::max(a.active, 0), 420, !a.mods.empty()); picked >= 0) {
        a.active = picked;
        Refresh(a, a.mods[picked].id);
    }
    ImGui::SameLine();
    if (Mod* mod = Active(a)) {
        if (mod->enabled) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kGreenTop), "%s", Tr("Activado en el juego", "Switched on in the game"));
            ImGui::SameLine();
            if (ui::Pill(Tr("Desactivar", "Switch off"), ui::kGreyTop, ui::kGreyBottom, true)) {
                tstudio::SetModEnabled(a.paths.config, mod->id, false);
                Refresh(a);
            }
        } else {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kOrangeTop), "%s", Tr("Desactivado: no se usa", "Switched off: not used"));
            ImGui::SameLine();
            if (ui::Pill(Tr("Activar", "Switch on"), ui::kBlueTop, ui::kBlueBottom, true)) {
                tstudio::SetModEnabled(a.paths.config, mod->id, true);
                Refresh(a);
            }
        }
        ImGui::SameLine();
    }
    if (ui::Pill(Tr("+ Nuevo mod", "+ New mod"), ui::kGreenTop, ui::kGreenBottom, true)) {
        std::snprintf(a.newName, sizeof a.newName, "%s", Tr("Temporada MLB", "MLB Season"));
        a.newError.clear();
        a.openNew = true;
    }
}

// One setting: its name, its choice, and what it means right below.
int SettingRow(const char* label, const char* help, const std::vector<std::string>& items, int current, bool enabled = true) {
    ImGui::Spacing();
    Heading(label);
    ImGui::SameLine(300);
    const int picked = Choice((std::string("##") + label).c_str(), items, current, 320, enabled);
    ImGui::Indent(12);
    Muted(help);
    ImGui::Unindent(12);
    ImGui::Spacing();
    ImGui::Separator();
    return picked;
}

void DrawSettings(App& a) {
    Mod* mod = Active(a);
    if (!mod) {
        ImGui::TextWrapped("%s", Tr("No hay ningún mod. Crea uno con «+ Nuevo mod» arriba: guarda los ajustes y los calendarios de tu temporada.",
                                    "There are no mods. Create one with «+ New mod» above: it keeps your season's settings and schedules."));
        return;
    }
    std::vector<std::string> starts{Tr("2008 (original)", "2008 (original)")};
    for (int y = 2009; y <= 2030; ++y) starts.push_back(std::to_string(y));
    const int startNow = mod->startYear > 2008 ? std::min(mod->startYear - 2008, 22) : 0;
    if (const int p = SettingRow(Tr("Año de inicio", "Start year"),
                                 Tr("En qué año empieza una partida nueva con este mod, en el modo Temporada y en MLB Life. "
                                    "2008 es el del juego. Las partidas ya empezadas siguen en su año.",
                                    "Which year a new game with this mod starts in, in Season mode and MLB Life. 2008 is the "
                                    "game's. Games already started stay in their year."),
                                 starts, startNow);
        p >= 0) {
        EditManifest(a, [&](std::string t) { return season_pack::SetKey(t, "start_year", p ? std::to_string(2008 + p) : ""); });
        return;  // Refresh moved the mods
    }

    std::vector<std::string> rosters{std::string(Tr("Mismo que el inicio (", "Same as the start (")) +
                                     std::to_string(mod->startYear ? mod->startYear : 2008) + ")"};
    for (int y = 2008; y <= 2030; ++y) rosters.push_back(std::to_string(y));
    const int rosterNow = mod->rosterYear ? std::clamp(mod->rosterYear - 2007, 1, 23) : 0;
    if (const int p = SettingRow(Tr("Año del roster (edades)", "Roster year (ages)"),
                                 Tr("De qué año es el roster del mod. Solo sirve para que las edades salgan bien. Lo normal es "
                                    "«Mismo que el inicio» (un roster de 2025 que empieza en 2025). Un roster de 2023 jugado "
                                    "desde 2026 lleva 2023.",
                                    "Which year the mod's roster is from. It is only used so ages come out right. The usual "
                                    "choice is «Same as the start» (a 2025 roster starting in 2025). A 2023 roster played from "
                                    "2026 takes 2023."),
                                 rosters, rosterNow);
        p >= 0) {
        EditManifest(a, [&](std::string t) { return season_pack::SetKey(t, "roster_year", p ? std::to_string(2007 + p) : ""); });
        return;  // Refresh moved the mods
    }

    const std::vector<std::string> dh{Tr("Como en MEJORAS", "As in ENHANCEMENTS"), Tr("En ambas ligas", "In both leagues"),
                                      Tr("Original (Liga Americana)", "Original (American League)")};
    const int dhNow = mod->universalDh ? 1 : mod->originalDh ? 2 : 0;
    if (const int p = SettingRow(Tr("DH (bateador designado)", "DH (designated hitter)"),
                                 Tr("Como en MEJORAS: lo decide la opción DH EN AMBAS LIGAS del launcher.\n"
                                    "En ambas ligas: DH en todos los partidos, como desde 2022.\n"
                                    "Original: como el juego, DH solo en los estadios de la Liga Americana.",
                                    "As in ENHANCEMENTS: the launcher's DH IN BOTH LEAGUES option decides.\n"
                                    "In both leagues: the DH in every game, as since 2022.\n"
                                    "Original: as the game, the DH only in American League parks."),
                                 dh, dhNow);
        p >= 0) {
        EditManifest(a, [&](std::string t) { return season_pack::SetKey(t, "dh", p == 1 ? "\"both\"" : p == 2 ? "\"original\"" : ""); });
        return;  // Refresh moved the mods
    }

    const bool own = mod->hasDivisions && !season_pack::HouOnly(*mod);
    const std::vector<std::string> leagues =
        own ? std::vector<std::string>{Tr("Propias (en su mod.toml)", "Its own (in its mod.toml)")}
            : std::vector<std::string>{Tr("Como en MEJORAS", "As in ENHANCEMENTS"), Tr("15 y 15", "15 and 15"),
                                       Tr("Original (14 y 16)", "Original (14 and 16)")};
    const int leaguesNow = own ? 0 : mod->hasDivisions ? 1 : mod->originalLeagues ? 2 : 0;
    if (const int p = SettingRow(Tr("Ligas", "Leagues"),
                                 Tr("Como en MEJORAS: lo decide la opción LIGAS 15 Y 15 del launcher.\n"
                                    "15 y 15: Houston en la AL West, como desde 2013.\n"
                                    "Original: las divisiones de 2008, como el juego.\n"
                                    "Con Start (no Quick Start), la pantalla Custom League del juego se abre ya así.",
                                    "As in ENHANCEMENTS: the launcher's 15 AND 15 LEAGUES option decides.\n"
                                    "15 and 15: Houston in the AL West, as since 2013.\n"
                                    "Original: 2008's divisions, as the game.\n"
                                    "With Start (not Quick Start), the game's Custom League screen already opens like this."),
                                 leagues, leaguesNow, !own);
        p >= 0) {
        EditManifest(a, [&](std::string t) {
            t = season_pack::SetModernDivisions(t, p == 1);
            return season_pack::SetKey(t, "leagues", p == 2 ? "\"original\"" : "");
        });
        return;
    }

    // Real schedules.
    ImGui::Spacing();
    Heading(Tr("Calendarios reales", "Real schedules"));
    Muted(Tr("Descarga los calendarios de las Grandes Ligas de los años que elijas (2002 a 2026; 2020 no está: tuvo 60 "
             "partidos) y los guarda en este mod. Cada uno se usa en la temporada de su año; un año sin el suyo tiene "
             "uno generado por el juego. Si el mod no tenía año de inicio, se le pone el primero. Hace falta internet.",
             "Downloads the major leagues' schedules for the years you choose (2002 to 2026; 2020 is missing: it had 60 "
             "games) and saves them in this mod. Each one is used in its year's season; a year without its own gets one "
             "generated by the game. If the mod had no start year, it gets the first one. Internet is needed."));
    std::vector<std::string> years;
    for (int y : season_schedule_mlb::OfferedYears()) years.insert(years.begin(), std::to_string(y));
    auto index = [&](int year) {
        for (size_t i = 0; i < years.size(); ++i)
            if (years[i] == std::to_string(year)) return static_cast<int>(i);
        return static_cast<int>(years.size()) - 1;
    };
    ImGui::TextUnformatted(Tr("Desde", "From"));
    ImGui::SameLine();
    if (const int p = Choice("##from", years, index(a.fromYear), 110); p >= 0) a.fromYear = std::atoi(years[p].c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(Tr("hasta", "to"));
    ImGui::SameLine();
    if (const int p = Choice("##to", years, index(a.toYear), 110); p >= 0) a.toYear = std::atoi(years[p].c_str());
    ImGui::SameLine();
    if (a.download.running) {
        std::lock_guard lock(a.download.mutex);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kYellow), "%s", a.download.status.c_str());
    } else if (ui::Pill(Tr("Descargar", "Download"), ui::kOrangeTop, ui::kOrangeBottom, true)) {
        a.download.lines.clear();
        a.download.error.clear();
        a.download.first = 0;
        a.download.done = false;
        a.download.running = true;
        if (a.download.thread.joinable()) a.download.thread.join();
        a.download.thread = std::thread(RunDownload, std::ref(a.download), a.fromYear, a.toYear, mod->root);
    }
    if (!a.downloadResult.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(a.downloadResult.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    if (mod->years.empty()) {
        Muted(Tr("Este mod todavía no tiene calendarios.", "This mod has no schedules yet."));
    } else {
        ImGui::TextUnformatted(Tr("En este mod:", "In this mod:"));
        for (int y : mod->years) {
            ImGui::SameLine();
            if (ui::Pill(std::to_string(y).c_str(), ui::kBlueTop, ui::kBlueBottom, true)) {
                a.calYear = y;
                a.tab = 1;
                LoadCalendar(a);
            }
        }
        Muted(Tr("Pulsa un año para verlo en CALENDARIO.", "Press a year to see it in SCHEDULE."));
    }
}

void DrawCalendar(App& a, ImVec2 size) {
    const Mod* mod = Active(a);
    if (!mod || mod->years.empty()) {
        ImGui::TextWrapped("%s", Tr("Este mod no tiene calendarios. Descárgalos en AJUSTES > Calendarios reales.",
                                    "This mod has no schedules. Download them in SETTINGS > Real schedules."));
        return;
    }
    std::vector<std::string> years;
    int yearIndex = 0;
    for (size_t i = 0; i < mod->years.size(); ++i) {
        years.push_back(std::to_string(mod->years[i]));
        if (mod->years[i] == a.calYear) yearIndex = static_cast<int>(i);
    }
    ImGui::TextUnformatted(Tr("Año", "Year"));
    ImGui::SameLine();
    if (const int p = Choice("##year", years, yearIndex, 100); p >= 0) {
        a.calYear = mod->years[p];
        LoadCalendar(a);
    }
    ImGui::SameLine(0, 24);
    ImGui::TextUnformatted(Tr("Equipo", "Team"));
    ImGui::SameLine();
    std::vector<std::string> teams{Tr("Todos", "All")};
    std::vector<int> order(30);
    for (int t = 0; t < 30; ++t) order[t] = t;
    std::sort(order.begin(), order.end(), [&](int x, int y) { return std::string(TeamCode(x, a.calYear)) < TeamCode(y, a.calYear); });
    int teamIndex = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        teams.push_back(std::string(TeamCode(order[i], a.calYear)) + " - " + TeamName(order[i], a.calYear));
        if (order[i] == a.team) teamIndex = static_cast<int>(i) + 1;
    }
    if (const int p = Choice("##team", teams, teamIndex, 240); p >= 0) a.team = p == 0 ? -1 : order[p - 1];
    static const char* kMonthsEs[] = {"Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct"};
    static const char* kMonthsEn[] = {"Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct"};
    for (int m = 3; m <= 10; ++m) {
        if (m > 3) ImGui::SameLine();
        const bool on = a.month == m;
        if (ui::Pill(Tr(kMonthsEs[m - 3], kMonthsEn[m - 3]), on ? ui::kOrangeTop : ui::kBlueTop, on ? ui::kOrangeBottom : ui::kBlueBottom, true)) {
            a.month = m;
            a.pickedDay = -1;
        }
    }
    Muted(a.calNote.c_str());

    // The month: a week a row, Sunday first.
    static constexpr int kMonthFirst[13] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366};
    const int firstIndex = kMonthFirst[a.month - 1] - season_schedule::kFirstDay;
    const int days = kMonthFirst[a.month] - kMonthFirst[a.month - 1];
    const int weekday = season_schedule_mlb::Weekday(a.calYear, a.month, 1);
    std::vector<std::vector<const season_schedule::Game*>> byDay(days);
    for (const auto& g : a.games)
        if (g.day >= firstIndex && g.day < firstIndex + days && (a.team < 0 || g.home == a.team || g.away == a.team))
            byDay[g.day - firstIndex].push_back(&g);
    const float listW = 300, gridW = size.x - listW - 16;
    const float cellW = (gridW - 6 * 4) / 7, top = ImGui::GetCursorPosY();
    const float cellH = std::max(46.0f, (size.y - (top - ImGui::GetCursorStartPos().y) - 30) / 6 - 4);
    ImGui::BeginChild("##grid", ImVec2(gridW, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    static const char* kDaysEs[] = {"Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"};
    static const char* kDaysEn[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    for (int c = 0; c < 7; ++c)
        dl->AddText(ImVec2(origin.x + c * (cellW + 4) + 6, origin.y), ui::kHeaderText, Tr(kDaysEs[c], kDaysEn[c]));
    const float gridTop = origin.y + 24;
    for (int d = 0; d < days; ++d) {
        const int slot = d + weekday, row = slot / 7, col = slot % 7;
        const ImVec2 p(origin.x + col * (cellW + 4), gridTop + row * (cellH + 4));
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(d);
        const bool clicked = ImGui::InvisibleButton("##day", ImVec2(cellW, cellH));
        ImGui::PopID();
        const bool picked = a.pickedDay == firstIndex + d, hot = ImGui::IsItemHovered();
        const auto& games = byDay[d];
        dl->AddRectFilled(p, ImVec2(p.x + cellW, p.y + cellH), picked ? ui::kBlueBottom : hot ? ui::kPanelHeader : ui::kPanel, 6);
        dl->AddRect(p, ImVec2(p.x + cellW, p.y + cellH), ui::kPanelBorder, 6);
        dl->AddText(ImVec2(p.x + 6, p.y + 4), games.empty() ? ui::kMuted : IM_COL32_WHITE, std::to_string(d + 1).c_str());
        std::string line;
        if (a.team >= 0 && !games.empty()) {
            const auto* g = games.front();
            line = (g->home == a.team ? std::string("vs ") + TeamCode(g->away, a.calYear) : std::string("@ ") + TeamCode(g->home, a.calYear));
            if (games.size() > 1) line += " (2)";
        } else if (!games.empty()) {
            line = std::to_string(games.size()) + Tr(" partidos", " games");
        }
        if (!line.empty()) dl->AddText(ImVec2(p.x + 6, p.y + cellH - 24), ui::kYellow, line.c_str());
        if (clicked) a.pickedDay = firstIndex + d;
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, gridTop + 6 * (cellH + 4)));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();

    // The picked day's games.
    ImGui::SameLine();
    ui::BeginPanel("##day", Tr("PARTIDOS DEL DÍA", "GAMES OF THE DAY"), ImVec2(listW, 0));
    if (a.pickedDay < 0) {
        Muted(Tr("Pulsa un día del calendario.", "Press a day on the calendar."));
    } else {
        const auto [m, d] = MonthDay(a.pickedDay);
        if (power_pros_ui::g_lang == power_pros_ui::Lang::En) ImGui::Text("%d/%d/%d", m, d, a.calYear);
        else ImGui::Text("%d/%d/%d", d, m, a.calYear);
        int shown = 0;
        for (const auto& g : a.games) {
            if (g.day != a.pickedDay || (a.team >= 0 && g.home != a.team && g.away != a.team)) continue;
            ImGui::Text("%s @ %s  %s", TeamCode(g.away, a.calYear), TeamCode(g.home, a.calYear), Clock(g.minutes).c_str());
            ++shown;
        }
        if (!shown) Muted(Tr("Sin partidos.", "No games."));
        Muted(Tr("Visitante @ local, hora del Este de EE. UU.", "Away @ home, US Eastern time."));
    }
    ui::EndPanel();
}

void DrawNewModPopup(App& a) {
    if (a.openNew) ImGui::OpenPopup("##newmod"), a.openNew = false;
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (ImGui::BeginPopupModal("##newmod", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
        Heading(Tr("Nuevo mod de temporada", "New season mod"));
        Muted(Tr("Una carpeta nueva en mods con su mod.toml, activada en el juego. Luego eliges su año, DH, ligas y calendarios.",
                 "A new folder in mods with its mod.toml, switched on in the game. Then you choose its year, DH, leagues and schedules."));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##name", a.newName, sizeof a.newName);
        if (!a.newError.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kRedTop), "%s", a.newError.c_str());
        if (ui::Pill(Tr("Crear", "Create"), ui::kGreenTop, ui::kGreenBottom) && CreateMod(a, a.newName)) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ui::Pill(Tr("Cancelar", "Cancel"), ui::kGreyTop, ui::kGreyBottom)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 12;
    s.FrameRounding = 8;
    s.PopupRounding = 8;
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
    c[ImGuiCol_FrameBgActive] = f(ui::kPanelHeader);
    c[ImGuiCol_Header] = f(ui::kBlueBottom);
    c[ImGuiCol_HeaderHovered] = f(ui::kPanelHeader);
    c[ImGuiCol_Button] = f(ui::kBlueBottom);
    c[ImGuiCol_ButtonHovered] = f(ui::kPanelHeader);
    c[ImGuiCol_Text] = f(IM_COL32_WHITE);
    c[ImGuiCol_Separator] = f(ui::kPanelBorder);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_ScrollbarBg] = f(ui::kPanel);
}

// ---- Checks without a window --------------------------------------------------------------------
int Selftest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) std::fprintf(stderr, "FAIL: %s\n", what), ++failures;
    };
    const fs::path dir = fs::temp_directory_path() / "season_studio_selftest";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "B pack" / "schedule", ec);
    fs::create_directories(dir / "A pack", ec);
    std::ofstream(dir / "A pack" / "mod.toml") << "name = \"A pack\"\nstart_year = 2025\ndh = \"original\"\nleagues = \"original\"\n";
    std::ofstream(dir / "B pack" / "mod.toml") << "name = \"B pack\"\nstart_year = 2026\n\n[divisions]\nal_west = [\"HOU\"]\n";
    std::ofstream(dir / "B pack" / "schedule" / "2026.csv") << "date,home,away\n2026-04-01,NYY,BOS\n";
    const auto mods = Discover(dir, {"A pack", "B pack"});
    check(mods.size() == 2 && mods[0].name == "A pack" && mods[0].startYear == 2025 && mods[0].originalDh &&
              mods[0].originalLeagues,
          "A pack read");
    check(mods[1].hasDivisions && season_pack::HouOnly(mods[1]) && mods[1].years == std::vector<int>{2026}, "B pack read");
    const auto plan = season_pack::Make(mods, true, true, [](const Mod& m) { return m.years; });
    check(plan.startYear == 2025 && !plan.dh && !plan.modernLeagues && plan.conflicts.size() == 2, "plan: the first by name");
    check(!season_text::DescribeSeason(plan, 10).help.empty(), "summary text");
    check(MonthDay(18) == std::pair<int, int>(3, 19) && MonthDay(214) == std::pair<int, int>(10, 1), "dates");
    fs::remove_all(dir, ec);
    if (!failures) std::printf("season studio selftest: ok\n");
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args = WideArgs(argc, argv);
    for (const auto& arg : args)
        if (arg == "--selftest") return Selftest();

    auto app = std::make_unique<App>();
    App& a = *app;
    a.paths = ResolvePaths(args);
    const auto lang = fs::exists(a.paths.config) ? tstudio::ReadSetting(a.paths.config, "ui", "language") : std::nullopt;
    power_pros_ui::g_lang = power_pros_ui::LangFromSetting(lang.value_or("auto"));
    std::string startMod;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--lang") power_pros_ui::g_lang = power_pros_ui::LangFromSetting(args[i + 1]);  // this session only
        if (args[i] == "--mod") startMod = args[i + 1];
    }
    // --download <from> <to> --mod <id>: the download without a window (tests).
    for (size_t i = 0; i + 2 < args.size(); ++i) {
        if (args[i] != "--download") continue;
        Refresh(a, startMod);
        if (!Active(a)) return 2;
        RunDownload(a.download, std::atoi(args[i + 1].c_str()), std::atoi(args[i + 2].c_str()), Active(a)->root);
        for (const auto& l : a.download.lines) std::printf("%s\n", l.c_str());
        if (!a.download.error.empty()) std::printf("%s\n", a.download.error.c_str());
        return a.download.error.empty() ? 0 : 1;
    }

#if defined(_WIN32)
    SetProcessDPIAware();
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window* window = SDL_CreateWindow("Power Pros Wii 2008 Season Studio", 1360, 860, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_SetWindowMinimumSize(window, 1100, 700);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    power_pros_ui::LoadFonts();
    ImGui::GetIO().FontDefault = power_pros_ui::g_menu;
    ApplyStyle();

    Refresh(a, startMod);
    // Opening view (screenshot tests): --tab calendar --year <y> --team <code> --month <m> --day <d>
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--tab") a.tab = args[i + 1] == "calendar" ? 1 : 0;
        if (args[i] == "--year") a.calYear = std::atoi(args[i + 1].c_str()), LoadCalendar(a);
        if (args[i] == "--month") a.month = std::clamp(std::atoi(args[i + 1].c_str()), 3, 10);
        if (args[i] == "--team") a.team = season_schedule::TeamNumber(args[i + 1]);
        if (args[i] == "--day") a.pickedDay = season_schedule::DayIndex(a.month, std::atoi(args[i + 1].c_str()));
    }

    bool quit = false;
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) quit = true;
        }
        if (a.download.done && a.download.running) {
            a.download.thread.join();
            a.download.running = false;
            std::string result;
            for (const auto& l : a.download.lines) result += (result.empty() ? "" : "\n") + l;
            if (!a.download.error.empty()) result += (result.empty() ? "" : "\n") + a.download.error;
            if (Mod* mod = Active(a); mod && a.download.first && !mod->startYear && a.download.first != 2008) {
                const int first = a.download.first;
                EditManifest(a, [&](std::string t) { return season_pack::SetKey(t, "start_year", std::to_string(first)); });
                result += std::string(Tr("\nAño de inicio del mod: ", "\nThe mod's start year: ")) + std::to_string(first) + ".";
            }
            a.downloadResult = result;
            Refresh(a);
            Toast(a, a.download.error.empty() ? Tr("Calendarios guardados en el mod.", "Schedules saved in the mod.")
                                              : Tr("Algún año no se pudo descargar.", "Some year could not be downloaded."),
                  !a.download.error.empty());
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ImVec2 vs = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(vs);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        DrawBand(a, vs.x);

        const float top = 76, footer = 58, gap = 10, left = 380;
        const float bodyH = vs.y - top - footer - gap;
        ImGui::SetCursorPos(ImVec2(10, top));
        DrawSummary(a, ImVec2(left, bodyH));
        ImGui::SetCursorPos(ImVec2(left + 20, top));
        ImGui::BeginChild("##main", ImVec2(vs.x - left - 30, bodyH));
        DrawModBar(a);
        ImGui::Spacing();
        for (int t = 0; t < 2; ++t) {
            if (t) ImGui::SameLine();
            const bool on = a.tab == t;
            const char* name = t == 0 ? Tr("AJUSTES", "SETTINGS") : Tr("CALENDARIO", "SCHEDULE");
            if (ui::Pill(name, on ? ui::kOrangeTop : ui::kBlueTop, on ? ui::kOrangeBottom : ui::kBlueBottom)) a.tab = t;
        }
        ImGui::Separator();
        const ImVec2 rest = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##tab", rest);
        if (a.tab == 0) DrawSettings(a);
        else DrawCalendar(a, rest);
        ImGui::EndChild();
        ImGui::EndChild();

        // Footer: the last message, the mod's folder, quit.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float fy = vs.y - footer;
        dl->AddRectFilled(ImVec2(0, fy), ImVec2(vs.x, vs.y), ui::kFooter);
        dl->AddLine(ImVec2(0, fy), ImVec2(vs.x, fy), ui::kBlueBottom, 3);
        ImGui::SetCursorScreenPos(ImVec2(10, fy + 6));
        if (Mod* mod = Active(a); mod && ui::Pill(Tr("Abrir carpeta del mod", "Open the mod's folder"), ui::kBlueTop, ui::kBlueBottom, true))
            OpenFolder(mod->root);
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(a.toastError ? ui::kRedTop : ui::kMuted), "%s", a.toast.c_str());
        ImGui::SetCursorScreenPos(ImVec2(vs.x - 110, fy + 6));
        if (ui::Pill(Tr("Salir", "Quit"), ui::kGreyTop, ui::kGreyBottom, true)) quit = true;
        DrawNewModPopup(a);
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 16, 24, 38, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    if (a.download.thread.joinable()) a.download.thread.join();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
