// SPDX-License-Identifier: GPL-3.0-or-later
#include "core.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace cstudio {
using tstudio::Utf8;
using tstudio::PathFromUtf8;
static void Require(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }

Preview PreviewText(const std::string& text) {
    Preview p;
    const auto codes = callnames::ToGameText(text, p.dropped);
    p.count = codes.size();
    const std::string punctuation = " .;?!/'()+-=";
    const int numbers[] = {0,4,7,8,9,30,38,41,42,59,60,64};
    for (char16_t code : codes) {
        char c = '?';
        if (code >= 220 && code <= 245) c = char('A' + code - 220);
        else if (code >= 252 && code <= 277) c = char('a' + code - 252);
        else if (code >= 203 && code <= 212) c = char('0' + code - 203);
        else for (size_t i = 0; i < punctuation.size(); ++i) if (code == numbers[i]) c = punctuation[i];
        p.text += c;
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || c == '.' || c == '-') p.upper += c;
    }
    return p;
}

Value Load(const fs::path& mod) {
    const auto file = mod / "callnames/callnames.toml";
    if (!fs::exists(file)) return Value(Value::table_type{{"callname", Value::array_type{}}});
    std::ifstream in(file, std::ios::binary);
    auto doc = toml::parse(in, Utf8(file));
    Require(doc.contains("callname") && doc.at("callname").is_array(), "Expected [[callname]] / Se esperaba [[callname]]");
    for (const auto& entry : doc.at("callname").as_array()) Require(entry.is_table(), "Invalid callname table / Tabla callname no válida");
    return doc;
}

bool SafeRelative(const fs::path& folder, const std::string& name) {
    auto path = PathFromUtf8(name);
    if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
    for (const auto& part : path) if (part == "..") return false;
    const auto root = fs::weakly_canonical(folder), full = fs::weakly_canonical(folder / path);
    auto a = root.begin(), b = full.begin();
    for (; a != root.end(); ++a, ++b) if (b == full.end() || *a != *b) return false;
    return true;
}

void Validate(const fs::path& mod, const Value& entry) {
    const auto voice = toml::find<int64_t>(entry, "voice");
    Require(voice >= callnames::kFirstVoice && voice <= callnames::kLastVoice, "Voice / Voz: 7223-10239");
    const auto p = PreviewText(toml::find<std::string>(entry, "list"));
    Require(p.count > 0 && p.count <= 15, "List / Lista: 1-15 caracteres visibles / visible characters");
    for (const char* key : {"normal", "excited", "full"}) {
        const auto name = toml::find_or<std::string>(entry, key, "");
        if (name.empty() && std::string(key) != "normal") continue;
        Require(SafeRelative(mod / "callnames", name), "Audio fuera del mod / Audio outside mod");
        auto file = mod / "callnames" / PathFromUtf8(name);
        std::string ext = Utf8(file.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return char(std::tolower(c)); });
        Require(ext == ".wav" || ext == ".mp3", "Audio: WAV / MP3");
        std::vector<int16_t> samples;
        std::string error;
        if (!callnames::ReadAudio(file, samples, error)) throw std::runtime_error(std::string(key) + ": " + error);
    }
}

static void AtomicWrite(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    auto temp = file; temp += ".studio.tmp";
#ifdef _WIN32
    FILE* out = _wfopen(temp.c_str(), L"wbx");
#else
    FILE* out = std::fopen(temp.c_str(), "wbx");
#endif
    Require(out != nullptr, "Cannot create temporary file / No se pudo crear temporal: " + Utf8(temp));
    const bool written = std::fwrite(text.data(), 1, text.size(), out) == text.size() && std::fflush(out) == 0;
    const bool closed = std::fclose(out) == 0;
    if (!written || !closed) { fs::remove(temp); throw std::runtime_error("Cannot write / No se pudo escribir: " + Utf8(temp)); }
#ifdef _WIN32
    const bool replaced = MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!replaced) fs::remove(temp);
    Require(replaced, "Cannot replace / No se pudo reemplazar: " + Utf8(file));
#else
    fs::rename(temp, file);
#endif
}

void Save(const fs::path& mod, const Value& document) {
    Require(SafeRelative(mod, "callnames/callnames.toml"), "Unsafe manifest path / Ruta no segura");
    std::set<int64_t> voices;
    for (const auto& e : document.at("callname").as_array()) {
        Validate(mod, e);
        Require(voices.insert(toml::find<int64_t>(e, "voice")).second, "Duplicate voice / Voz repetida");
    }
    auto printable = document;
    // toml11 retains literal-string formatting on assignment, including when new text contains an apostrophe.
    std::function<void(Value&)> basicStrings = [&](Value& v) {
        if (v.is_string()) v.as_string_fmt().fmt = toml::string_format::basic;
        else if (v.is_array()) for (auto& e : v.as_array()) basicStrings(e);
        else if (v.is_table()) for (auto& [key,e] : v.as_table()) basicStrings(e);
    };
    basicStrings(printable);
    const std::string text = toml::format(printable);
    std::istringstream check(text); (void)toml::parse(check);
    AtomicWrite(mod / "callnames/callnames.toml", text);
}

std::set<int> UsedVoices(const fs::path& mods, const fs::path& editing, int skip) {
    std::set<int> used;
    if (!fs::exists(mods)) return used;
    for (const auto& folder : fs::directory_iterator(mods)) {
        if (!folder.is_directory()) continue;
        const auto doc = Load(folder.path()); // An unreadable mod must not silently allocate a conflicting voice.
        int index = 0;
        for (const auto& e : doc.at("callname").as_array()) {
            if (!editing.empty() && fs::equivalent(folder.path(), editing) && index++ == skip) continue;
            auto v = toml::find_or<int64_t>(e, "voice", 0);
            if (v >= callnames::kFirstVoice && v <= callnames::kLastVoice) used.insert(int(v));
        }
    }
    return used;
}
int NextVoice(const std::set<int>& used) {
    for (int v = callnames::kFirstVoice; v <= callnames::kLastVoice; ++v) if (!used.contains(v)) return v;
    throw std::runtime_error("No free voices / No quedan voces libres");
}

std::string ImportAudio(const fs::path& mod, const fs::path& source) {
    std::string ext = Utf8(source.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    Require(ext == ".wav" || ext == ".mp3", "Audio: WAV / MP3");
    std::vector<int16_t> samples; std::string error;
    if (!callnames::ReadAudio(source, samples, error)) throw std::runtime_error(error);
    const auto folder = mod / "callnames";
    Require(SafeRelative(mod, "callnames"), "Unsafe callnames folder / Carpeta callnames no segura");
    fs::create_directories(folder);
    if (fs::weakly_canonical(source.parent_path()) == fs::weakly_canonical(folder)) return Utf8(source.filename());
    for (int i = 1;; ++i) {
        const auto name = "audio-" + std::to_string(i) + ext;
        if (fs::exists(folder / name)) continue;
        if (fs::copy_file(source, folder / name, fs::copy_options::none)) return name;
    }
}

void CreateMod(const fs::path& mods, const std::string& name) {
    Require(!name.empty() && name != "." && name != ".." && name.back() != '.' && name.back() != ' ' &&
            name.find_first_of("\\/:*?\"<>|\r\n\t") == std::string::npos, "Nombre de mod no válido / Invalid mod name");
    const auto folder = mods / PathFromUtf8(name);
    Require(!fs::exists(folder), "Mod already exists / El mod ya existe");
    Require(SafeRelative(mods, name), "Unsafe mod folder / Carpeta no segura");
    fs::create_directories(folder);
    AtomicWrite(folder / "mod.toml", toml::format(Value(Value::table_type{{"name", name}, {"description", "Callname Studio"}})));
}
}
