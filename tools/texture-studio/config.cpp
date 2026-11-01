// SPDX-License-Identifier: GPL-3.0-or-later
// Reads and writes single keys of Config.toml line by line, like the runtime's WriteSetting: comments
// and every other line stay as they are.
#include "config.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace tstudio {
namespace {
std::string Trim(std::string_view s) {
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string_view::npos ? std::string() : std::string(s.substr(a, b - a + 1));
}
std::vector<std::string> Lines(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}
std::string WithoutComment(std::string_view text) {
    char quote = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (quote) {
            if (text[i] == '\\' && quote == '"') ++i;
            else if (text[i] == quote) quote = 0;
        } else if (text[i] == '"' || text[i] == '\'') quote = text[i];
        else if (text[i] == '#') return Trim(text.substr(0, i));
    }
    return Trim(text);
}
// The whole value, including multiline enabled arrays; malformed arrays are never overwritten.
std::optional<size_t> ValueEnd(const std::vector<std::string>& lines, size_t first) {
    const auto initial = Trim(std::string_view(lines[first]).substr(lines[first].find('=') + 1));
    if (initial.empty() || initial.front() != '[') return first + 1;
    int depth = 0;
    char quote = 0;
    for (size_t i = first; i < lines.size(); ++i) {
        const auto text = WithoutComment(i == first ? initial : lines[i]);
        for (size_t k = 0; k < text.size(); ++k) {
            const char c = text[k];
            if (quote) {
                if (c == '\\' && quote == '"') ++k;
                else if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') quote = c;
            else if (c == '[') ++depth;
            else if (c == ']' && --depth == 0) return i + 1;
        }
    }
    return std::nullopt;
}
// [first, end) of the section's lines (after its header); first == end == lines.size() if absent.
std::pair<size_t, size_t> Section(const std::vector<std::string>& lines, std::string_view section) {
    size_t start = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string t = WithoutComment(lines[i]);
        if (t.size() < 2 || t.front() != '[' || t.back() != ']') continue;
        if (start != lines.size()) return {start, i};
        if (Trim(std::string_view(t).substr(1, t.size() - 2)) == section) start = i + 1;
    }
    return {start, lines.size()};
}
size_t KeyLine(const std::vector<std::string>& lines, std::pair<size_t, size_t> range, std::string_view key) {
    for (size_t i = range.first; i < range.second; ++i) {
        const size_t eq = lines[i].find('=');
        if (eq != std::string::npos && Trim(std::string_view(lines[i]).substr(0, eq)) == key) return i;
    }
    return lines.size();
}
// The value after '=': a basic "string" (escapes resolved), a literal 'string', an [array] (kept raw for
// ReadEnabledMods) or a bare value; a trailing # comment outside quotes is dropped.
std::string Value(std::string_view v) {
    const std::string t = Trim(v);
    if (t.empty()) return t;
    if (t.front() == '"') {
        std::string out;
        for (size_t i = 1; i < t.size() && t[i] != '"'; ++i) {
            if (t[i] == '\\' && i + 1 < t.size()) {
                const char e = t[++i];
                out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
            } else {
                out += t[i];
            }
        }
        return out;
    }
    if (t.front() == '\'') return t.substr(1, t.find('\'', 1) == std::string::npos ? std::string::npos : t.find('\'', 1) - 1);
    return WithoutComment(t);
}
} // namespace

std::optional<std::string> ReadSetting(const std::filesystem::path& config, std::string_view section, std::string_view key) {
    const auto lines = Lines(config);
    const size_t i = KeyLine(lines, Section(lines, section), key);
    if (i == lines.size()) return std::nullopt;
    const auto end = ValueEnd(lines, i);
    if (!end) return std::nullopt;
    std::string value = WithoutComment(std::string_view(lines[i]).substr(lines[i].find('=') + 1));
    for (size_t j = i + 1; j < *end; ++j) value += " " + WithoutComment(lines[j]);
    return Value(value);
}

std::vector<std::string> ReadEnabledMods(const std::filesystem::path& config) {
    std::vector<std::string> out;
    const auto value = ReadSetting(config, "mods", "enabled");
    if (!value) return out;
    for (size_t i = 0; i < value->size(); ++i) {
        const char q = (*value)[i];
        if (q != '"' && q != '\'') continue;
        std::string item;
        for (++i; i < value->size() && (*value)[i] != q; ++i) {
            if (q == '"' && (*value)[i] == '\\' && i + 1 < value->size()) ++i;
            item += (*value)[i];
        }
        out.push_back(item);
    }
    return out;
}

bool SetModEnabled(const std::filesystem::path& config, const std::string& id, bool enabled) {
    const auto lines = Lines(config);
    const size_t key = KeyLine(lines, Section(lines, "mods"), "enabled");
    if (key != lines.size() && !ValueEnd(lines, key)) return false;
    auto list = ReadEnabledMods(config);
    std::erase(list, id);
    if (enabled) list.push_back(id);
    std::string value = "[";
    for (size_t i = 0; i < list.size(); ++i) {
        value += i ? ", \"" : "\"";
        for (char c : list[i]) {
            if (c == '"' || c == '\\') value += '\\';
            value += c;
        }
        value += '"';
    }
    value += "]";
    return WriteSetting(config, "mods", "enabled", value);
}

bool WriteSetting(const std::filesystem::path& config, std::string_view section, std::string_view key, const std::string& value) {
    auto lines = Lines(config);
    auto range = Section(lines, section);
    if (range.first == lines.size()) {
        if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
        lines.emplace_back("[" + std::string(section) + "]");
        range = {lines.size(), lines.size()};
    }
    const std::string line = std::string(key) + " = " + value;
    const size_t i = KeyLine(lines, range, key);
    if (i == lines.size()) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(range.second), line);
    else {
        const auto end = ValueEnd(lines, i);
        if (!end) return false;
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i + 1), lines.begin() + static_cast<std::ptrdiff_t>(*end));
        lines[i] = line;
    }
    // Written to a temporary file and renamed, so a failure never leaves the game's config half-written.
    auto temp = config;
    temp += ".tmp";
#ifdef _WIN32
    FILE* out = _wfopen(temp.c_str(), L"wbx");
#else
    FILE* out = std::fopen(temp.c_str(), "wbx");
#endif
    if (!out) return false;
    std::string text;
    for (const auto& line : lines) text += line + "\r\n";
    const bool written = std::fwrite(text.data(), 1, text.size(), out) == text.size() && std::fflush(out) == 0;
    const bool closed = std::fclose(out) == 0;
    std::error_code ec;
    if (!written || !closed) { std::filesystem::remove(temp, ec); return false; }
#ifdef _WIN32
    const bool replaced = MoveFileExW(temp.c_str(), config.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    std::filesystem::rename(temp, config, ec);
    const bool replaced = !ec;
#endif
    if (!replaced) std::filesystem::remove(temp, ec);
    return replaced;
}

} // namespace tstudio
