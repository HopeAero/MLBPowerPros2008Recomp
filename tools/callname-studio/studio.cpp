// SPDX-License-Identifier: GPL-3.0-or-later
#include "core.h"
#include "ui.h"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
using namespace cstudio;
using tstudio::Utf8;
using tstudio::PathFromUtf8;
using power_pros_ui::Tr;
namespace ui = tstudio::ui;
struct App {
    fs::path mods, config, mod;
    std::vector<std::string> ids;
    Value doc;
    int selected = -1, editing = -1, voice = 7223;
    char list[1024]{}, newMod[256]{};
    std::string audio[3], error, notice;
    bool openEdit = false, loaded = false, picking = false;
    SDL_AudioStream* stream = nullptr;
};
const char* keys[] = {"normal", "excited", "full"};
const char* Saved() { return Tr("Guardado. Ahora asígnalos en PowerUp+ (MODS > EDITAR ROSTER): aparecen como «Callname <texto>». El mod debe estar activado; reinicia el juego.", "Saved. Now assign them in PowerUp+ (MODS > EDIT ROSTER): listed as 'Callname <text>'. Enable the mod and restart the game."); }
void LoadMod(App& a, const std::string& id) {
    a.loaded = false; a.selected = -1; a.error.clear();
    a.mod = a.mods / PathFromUtf8(id);
    a.doc = Load(a.mod); a.loaded = true;
}
void Discover(App& a) {
    a.ids.clear();
    if (fs::exists(a.mods)) for (const auto& f : fs::directory_iterator(a.mods)) if (f.is_directory()) a.ids.push_back(Utf8(f.path().filename()));
    std::sort(a.ids.begin(), a.ids.end());
}
void BeginEdit(App& a, int index) {
    a.editing = index; a.error.clear();
    a.voice = NextVoice(UsedVoices(a.mods, a.mod, index));
    a.list[0] = 0;
    for (auto& s : a.audio) s.clear();
    if (index >= 0) {
        const auto& e = a.doc.at("callname").at(size_t(index));
        a.voice = toml::find_or<int>(e, "voice", a.voice);
        std::snprintf(a.list,sizeof a.list,"%s",toml::find_or<std::string>(e,"list", "").c_str());
        for (int i = 0; i < 3; ++i) a.audio[i] = toml::find_or<std::string>(e,keys[i],"");
    }
    a.openEdit = true;
}
void CommitEdit(App& a) {
    auto doc = a.doc;
    Value entry = a.editing >= 0 ? doc.at("callname").at(size_t(a.editing)) : Value(Value::table_type{});
    entry["voice"] = a.voice; entry["list"] = std::string(a.list);
    for (int i = 0; i < 3; ++i) {
        if (a.audio[i].empty()) entry.as_table().erase(keys[i]);
        else entry[keys[i]] = a.audio[i];
    }
    if (a.editing >= 0) doc.at("callname").at(size_t(a.editing)) = entry;
    else doc.at("callname").as_array().push_back(entry);
    Save(a.mod,doc); a.doc = std::move(doc); a.notice = Saved(); a.error.clear();
}
void Play(App& a, const std::string& name) {
    if (!SafeRelative(a.mod / "callnames",name)) throw std::runtime_error("Audio fuera del mod / Audio outside mod");
    std::vector<int16_t> pcm; std::string error;
    if (!callnames::ReadAudio(a.mod / "callnames" / PathFromUtf8(name),pcm,error)) throw std::runtime_error(error);
    if (!a.stream) {
        SDL_AudioSpec spec{SDL_AUDIO_S16,1,int(callnames::kRate)};
        a.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
        if (!a.stream) throw std::runtime_error(SDL_GetError());
    }
    SDL_ClearAudioStream(a.stream);
    if (!SDL_PutAudioStreamData(a.stream,pcm.data(),int(pcm.size() * 2)) || !SDL_FlushAudioStream(a.stream) || !SDL_ResumeAudioStreamDevice(a.stream)) throw std::runtime_error(SDL_GetError());
}
struct Pick { int slot; std::string path, error; };
constexpr Uint32 kPicked = SDL_EVENT_USER;
void SDLCALL Picked(void* data, const char* const* files, int) {
    auto pick = static_cast<Pick*>(data);
    if (!files) pick->error = SDL_GetError();
    else if (*files) pick->path = *files;
    SDL_Event event{}; event.type = kPicked; event.user.data1 = pick;
    if (!SDL_PushEvent(&event)) delete pick;
}
void Muted(const char* text) { ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kMuted),"%s",text); }
bool Pill(const char* label, bool enabled = true) { return ui::Pill(label,ui::kBlueTop,ui::kBlueBottom,true,enabled); }

void DrawEdit(App& a) {
    if (a.openEdit) { ImGui::OpenPopup("##edit"); a.openEdit = false; }
    ImGui::SetNextWindowSize(ImVec2(820,670),ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##edit",nullptr,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) return;
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kYellow),"%s",Tr("EDITAR CALLNAME","EDIT CALLNAME"));
    ImGui::Spacing(); ImGui::SetNextItemWidth(480);
    ImGui::InputText(Tr("Texto de la lista","List text"),a.list,sizeof a.list);
    const auto p = PreviewText(a.list);
    ImGui::Text("%s: %s (%zu/15)",Tr("En el juego","In game"),p.text.c_str(),p.count);
    ImGui::Text("Search Type: %s",p.upper.c_str());
    if (!p.dropped.empty()) ImGui::TextWrapped("%s: %s",Tr("Caracteres descartados","Dropped characters"),p.dropped.c_str());
    ImGui::SetNextItemWidth(180); ImGui::InputInt(Tr("Número de voz","Voice number"),&a.voice);
    bool clash = false;
    try { clash = UsedVoices(a.mods,a.mod,a.editing).contains(a.voice); }
    catch (const std::exception& e) { a.error = e.what(); }
    if (clash) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kOrangeTop),"%s",Tr("Aviso: otra entrada o mod ya usa esta voz.","Warning: another entry or mod already uses this voice."));
    Muted(Tr("Voces: 7223-10239. Emocionado y Completo son opcionales: usan Normal.","Voices: 7223-10239. Excited and Full are optional: they use Normal."));
    ImGui::Separator();
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        ImGui::Text("%s",i == 0 ? "Normal" : i == 1 ? Tr("Emocionado","Excited") : Tr("Completo","Full"));
        Muted(a.audio[i].empty() ? Tr("Sin archivo","No file") : a.audio[i].c_str());
        if (Pill(Tr("Elegir WAV / MP3","Choose WAV / MP3"),!a.picking)) {
            static const SDL_DialogFileFilter filters[] = {{"WAV / MP3","wav;mp3"}};
            a.picking = true;
            SDL_ShowOpenFileDialog(Picked,new Pick{i},nullptr,filters,1,nullptr,false);
        }
        ImGui::SameLine();
        if (Pill(Tr("Escuchar","Listen"),!a.audio[i].empty())) try { Play(a,a.audio[i]); } catch(const std::exception& e) { a.error = e.what(); }
        if (i) { ImGui::SameLine(); if (Pill(Tr("Quitar","Clear"))) a.audio[i].clear(); }
        ImGui::PopID();
    }
    Muted(Tr("PCM WAV de 16 bits o MP3; mono/estéreo, 8-192 kHz, máximo 5 segundos.","16-bit PCM WAV or MP3; mono/stereo, 8-192 kHz, up to 5 seconds."));
    ImGui::TextWrapped("%s",a.error.c_str());
    if (Pill(Tr("Guardar","Save"),!a.picking && p.count > 0 && p.count <= 15 && a.voice >= 7223 && a.voice <= 10239 && !a.audio[0].empty())) {
        try { CommitEdit(a); ImGui::CloseCurrentPopup(); } catch(const std::exception& e) { a.error = e.what(); }
    }
    ImGui::SameLine(); if (Pill(Tr("Cancelar","Cancel"),!a.picking)) { a.error.clear(); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

void Draw(App& a, ImVec2 size, bool& quit) {
    using namespace power_pros_ui;
    auto dl = ImGui::GetWindowDrawList(); int start = dl->VtxBuffer.Size;
    dl->AddRectFilled(ImVec2(0,0),ImVec2(size.x,64),IM_COL32_WHITE);
    Shade(dl,start,0,64,ui::kBandTop,ui::kBandBottom);
    ShadowedText(dl,g_title,30,ImVec2(18,6),IM_COL32_WHITE,"POWER PROS WII 2008");
    Text(dl,g_bold,15,ImVec2(20,41),ui::kYellow,Tr("E D I T O R   D E   C A L L N A M E S","C A L L N A M E   E D I T O R"));
    ImGui::SetCursorScreenPos(ImVec2(size.x - 100,14));
    auto lang = g_lang;
    if (ui::LangSwitch(&lang)) { g_lang = lang; if (!tstudio::WriteSetting(a.config,"ui","language",lang == Lang::En ? "\"en\"" : "\"es\"")) a.error = Tr("No se pudo guardar el idioma.","Could not save language."); }
    ImGui::SetCursorPos(ImVec2(16,80));
    ui::BeginPanel("##mods",Tr("MOD Y CALLNAMES","MOD AND CALLNAMES"),ImVec2(size.x - 32,size.y - 200));
    ImGui::SetNextItemWidth(360);
    if (ImGui::BeginCombo("Mod",a.mod.empty() ? Tr("Elige un mod","Choose a mod") : Utf8(a.mod.filename()).c_str())) {
        for (const auto& id : a.ids) if (ImGui::Selectable(id.c_str())) try { LoadMod(a,id); } catch(const std::exception& e) { a.error = e.what(); }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (Pill(Tr("+ Nuevo mod","+ New mod"))) { a.error.clear(); ImGui::OpenPopup("##new"); }
    if (!a.mod.empty()) {
        ImGui::SameLine(); const auto enabled = tstudio::ReadEnabledMods(a.config); const auto id = Utf8(a.mod.filename());
        const bool on = std::find(enabled.begin(),enabled.end(),id) != enabled.end();
        if (Pill(on ? Tr("Desactivar","Disable") : Tr("Activar","Enable"))) {
            if (!tstudio::SetModEnabled(a.config,id,!on)) a.error = Tr("No se pudo guardar Config.toml.","Could not save Config.toml.");
        }
        ImGui::SameLine(); Muted(on ? Tr("Activado","Enabled") : Tr("Desactivado","Disabled"));
    }
    if (ImGui::BeginPopupModal("##new",nullptr,ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::InputText(Tr("Nombre del mod","Mod name"),a.newMod,sizeof a.newMod);
        ImGui::TextWrapped("%s",a.error.c_str());
        if (Pill(Tr("Crear y activar","Create and enable"))) try {
            CreateMod(a.mods,a.newMod); Discover(a); LoadMod(a,a.newMod);
            if (!tstudio::SetModEnabled(a.config,a.newMod,true)) throw std::runtime_error("Mod creado; no se pudo activar / Mod created; enable failed");
            ImGui::CloseCurrentPopup();
        } catch(const std::exception& e) { a.error = e.what(); }
        ImGui::SameLine(); if (Pill(Tr("Cancelar","Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Separator();
    if (Pill(Tr("Añadir","Add"),a.loaded)) try { BeginEdit(a,-1); } catch(const std::exception& e) { a.error = e.what(); }
    ImGui::SameLine(); if (Pill(Tr("Editar","Edit"),a.selected >= 0)) try { BeginEdit(a,a.selected); } catch(const std::exception& e) { a.error = e.what(); }
    ImGui::SameLine(); if (Pill(Tr("Borrar","Delete"),a.selected >= 0)) ImGui::OpenPopup("##delete");
    if (ImGui::BeginPopupModal("##delete",nullptr,ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextUnformatted(Tr("¿Borrar el callname? Los archivos de audio se conservan.","Delete the callname? Audio files are kept."));
        if (Pill(Tr("Borrar","Delete"))) try {
            auto doc = a.doc; auto& entries = doc.at("callname").as_array(); entries.erase(entries.begin() + a.selected);
            Save(a.mod,doc); a.doc = std::move(doc); a.selected = -1; a.notice = Saved(); ImGui::CloseCurrentPopup();
        } catch(const std::exception& e) { a.error = e.what(); }
        ImGui::SameLine(); if (Pill(Tr("Cancelar","Cancel"))) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
    }
    if (a.loaded && ImGui::BeginTable("##list",5,ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,ImVec2(0,0))) {
        for (const auto* label : {Tr("Voz","Voice"),Tr("Texto de la lista","List text"),"Normal",Tr("Emocionado","Excited"),Tr("Completo","Full")}) ImGui::TableSetupColumn(label);
        ImGui::TableHeadersRow(); int index = 0;
        for (const auto& e : a.doc.at("callname").as_array()) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::PushID(index);
            const auto label = std::to_string(toml::find_or<int>(e,"voice",0));
            if (ImGui::Selectable(label.c_str(),index == a.selected,ImGuiSelectableFlags_SpanAllColumns)) a.selected = index;
            ImGui::TableNextColumn(); ImGui::TextUnformatted(toml::find_or<std::string>(e,"list","").c_str());
            for (const char* key : keys) { ImGui::TableNextColumn(); ImGui::TextUnformatted(toml::find_or<std::string>(e,key,"- Normal").c_str()); }
            ImGui::PopID(); ++index;
        }
        ImGui::EndTable();
    }
    ui::EndPanel();
    ImGui::SetCursorPos(ImVec2(20,size.y - 106)); ImGui::PushTextWrapPos(size.x - 145);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kRedTop),"%s",a.error.c_str());
    ImGui::TextWrapped("%s",a.notice.empty() ? Tr("Crea voces con tu propio audio. Asígnalas después en PowerUp+. Activa el mod para usarlas.","Create callnames with your own audio. Assign them in PowerUp+ and enable the mod.") : a.notice.c_str());
    ImGui::PopTextWrapPos(); ImGui::SetCursorPos(ImVec2(size.x - 120,size.y - 65)); if (Pill(Tr("Salir","Quit"))) quit = true;
    DrawEdit(a);
}
}

int main(int argc, char** argv) try {
    std::vector<std::string> args;
#ifdef _WIN32
    int count = 0; auto wide = CommandLineToArgvW(GetCommandLineW(),&count);
    for (int i = 1; wide && i < count; ++i) args.push_back(Utf8(fs::path(wide[i])));
    if (wide) LocalFree(wide);
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    auto option = [&](const std::string& key, std::string fallback = "") { for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == key) return args[i + 1]; return fallback; };
    auto flag = [&](const std::string& key) { return std::find(args.begin(),args.end(),key) != args.end(); };
    if (flag("--selftest")) return Selftest();
    App a; const fs::path base = PathFromUtf8(SDL_GetBasePath());
    a.mods = PathFromUtf8(option("--mods",Utf8(base / "mods")));
    a.config = PathFromUtf8(option("--config",Utf8(base / "UserData/Config.toml")));
    fs::create_directories(a.config.parent_path());
    power_pros_ui::g_lang = power_pros_ui::LangFromSetting(option("--lang",tstudio::ReadSetting(a.config,"ui","language").value_or("auto")));
    const auto id = option("--mod");
    if (!id.empty() && !SafeRelative(a.mods,id)) throw std::runtime_error("Invalid mod path");
    if (flag("--create-mod")) { CreateMod(a.mods,id); if (!tstudio::SetModEnabled(a.config,id,true)) throw std::runtime_error("Could not enable mod"); }
    Discover(a);
    if (!id.empty()) LoadMod(a,id); else if (!a.ids.empty()) try { LoadMod(a,a.ids.front()); } catch(const std::exception& e) { a.error = e.what(); }
    if (flag("--add") || flag("--update")) {
        if (!a.loaded) throw std::runtime_error("--mod required");
        const int index = flag("--update") ? std::stoi(option("--update")) : -1;
        if (flag("--update") && (index < 0 || size_t(index) >= a.doc.at("callname").as_array().size())) throw std::runtime_error("Invalid entry index");
        BeginEdit(a,index);
        a.voice = std::stoi(option("--voice",std::to_string(a.voice)));
        std::snprintf(a.list,sizeof a.list,"%s",option("--list",a.list).c_str());
        for (int i = 0; i < 3; ++i) if (!option(std::string("--") + keys[i]).empty()) a.audio[i] = ImportAudio(a.mod,PathFromUtf8(option(std::string("--") + keys[i])));
        CommitEdit(a); std::printf("Saved voice %d\n",a.voice); return 0;
    }
    if (flag("--delete")) {
        const int index = std::stoi(option("--delete")); auto& entries = a.doc.at("callname").as_array();
        if (index < 0 || size_t(index) >= entries.size()) throw std::runtime_error("Invalid entry index");
        entries.erase(entries.begin()+index); Save(a.mod,a.doc); return 0;
    }
    if (flag("--validate")) { for (const auto& e : a.doc.at("callname").as_array()) Validate(a.mod,e); return 0; }
    if (flag("--create-mod")) return 0;
    if (flag("--edit")) BeginEdit(a,std::stoi(option("--edit")));
#ifdef _WIN32
    SetProcessDPIAware();
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) throw std::runtime_error(SDL_GetError());
    auto window = SDL_CreateWindow("Power Pros Wii 2008 Callname Studio",1200,820,SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) throw std::runtime_error(SDL_GetError());
    SDL_SetWindowMinimumSize(window,1100,800);
    auto renderer = SDL_CreateRenderer(window,nullptr); if (!renderer) throw std::runtime_error(SDL_GetError()); SDL_SetRenderVSync(renderer,1);
    ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL3_InitForSDLRenderer(window,renderer); ImGui_ImplSDLRenderer3_Init(renderer);
    power_pros_ui::LoadFonts(); ImGui::GetIO().FontDefault = power_pros_ui::g_menu;
    ImGui::StyleColorsDark(); auto& style = ImGui::GetStyle(); style.WindowRounding = 12; style.FrameRounding = 8; style.PopupRounding = 8;
    style.FramePadding = ImVec2(10,6); style.ItemSpacing = ImVec2(8,6);
    style.Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(ui::kWindow);
    style.Colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(ui::kPanel);
    bool quit = false;
    while (!quit) {
        SDL_Event event; while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) quit = true;
            if (event.type == kPicked) {
                auto pick = std::unique_ptr<Pick>(static_cast<Pick*>(event.user.data1));
                a.picking = false;
                try { if (!pick->error.empty()) throw std::runtime_error(pick->error); if (!pick->path.empty()) a.audio[pick->slot] = ImportAudio(a.mod,PathFromUtf8(pick->path)); a.error.clear(); }
                catch (const std::exception& e) { a.error = e.what(); }
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
        const auto size = ImGui::GetMainViewport()->Size; ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(size);
        ImGui::Begin("##root",nullptr,ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);
        Draw(a,size,quit); ImGui::End(); ImGui::Render();
        SDL_SetRenderDrawColor(renderer,16,24,38,255); SDL_RenderClear(renderer); ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer); SDL_RenderPresent(renderer);
    }
    if (a.stream) SDL_DestroyAudioStream(a.stream);
    ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 0;
} catch(const std::exception& e) { std::fprintf(stderr,"Callname Studio: %s\n",e.what()); return 1; }
