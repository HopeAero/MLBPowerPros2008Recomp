// SPDX-License-Identifier: GPL-3.0-or-later
#include "core.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>

namespace cstudio {
int Selftest() {
    const auto dir = fs::temp_directory_path() / ("callname-studio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::cout << (ok ? "PASS " : "FAIL ") << what << '\n'; if (!ok) ++failures; };
    auto rejects = [&](auto action) { try { action(); return false; } catch (...) { return true; } };
    auto wav = [&](const fs::path& file, int rate, int channels, int bits, int frames) {
        std::ofstream out(file, std::ios::binary);
        auto le = [&](uint32_t v, int bytes) { for (int i = 0; i < bytes; ++i) out.put(char(v >> (i * 8))); };
        int size = frames * channels * (bits / 8);
        out << "RIFF"; le(36 + size, 4); out << "WAVEfmt "; le(16,4); le(1,2); le(channels,2);
        le(rate,4); le(rate * channels * (bits / 8),4); le(channels * (bits / 8),2); le(bits,2); out << "data"; le(size,4);
        for (int i = 0; i < frames; ++i) for (int c = 0; c < channels; ++c) le(uint16_t(12000 * std::sin(i * 0.1)), bits / 8);
    };
    try {
        CreateMod(dir, "Uno"); CreateMod(dir, "Dos");
        const auto mod = dir / "Uno";
        wav(dir / "sine.wav", 22050, 1, 16, 2205);
        auto normal = ImportAudio(mod, dir / "sine.wav");
        std::ofstream(mod / "callnames/callnames.toml") << "# handwritten comment\nfuture = 42\n[[callname]] # entry comment\nvoice = 7223 # voice comment\nlist = 'Acuña'\nnormal = '" << normal << "'\ncustom = 'keep me'\n";
        auto doc = Load(mod);
        Save(mod, doc);
        auto round = Load(mod);
        check(toml::find<int>(round, "future") == 42 && toml::find<std::string>(round.at("callname").at(0), "custom") == "keep me", "TOML unknown keys retained");
        std::ifstream in(mod / "callnames/callnames.toml"); std::string text((std::istreambuf_iterator<char>(in)), {}); in.close();
        check(text.find("handwritten comment") != text.npos && text.find("entry comment") != text.npos && text.find("voice comment") != text.npos, "handwritten comments retained");
        auto& entry = round.at("callname").at(0);
        entry["list"] = std::string("José O'Neil"); Save(mod, round);
        check(toml::find<std::string>(Load(mod).at("callname").at(0), "list") == "José O'Neil", "UTF-8 round trip after editing");
        check(NextVoice(UsedVoices(dir)) == 7224 && NextVoice(UsedVoices(dir, mod, 0)) == 7223, "next voice includes disabled mods and excludes edited entry");
        auto other = Load(dir / "Dos"); other["callname"] = Value::array_type{Value(Value::table_type{{"voice",7224},{"list","Dos"},{"normal",ImportAudio(dir / "Dos",dir / "sine.wav")}})}; Save(dir / "Dos",other);
        check(NextVoice(UsedVoices(dir)) == 7225, "voices across two mods");
        const auto p = PreviewText("Ácuña 23!猫");
        check(p.text == "Acuna 23!" && p.upper == "ACUNA" && p.dropped == "猫" && p.count == 9, "runtime list preview and Search Type");
        entry["list"] = std::string(16, 'A'); check(rejects([&]{ Save(mod,round); }), "reject over 15 game characters"); entry["list"] = std::string(15,'A'); Save(mod,round);
        std::vector<int16_t> samples; std::string error;
        for (int rate : {8000,192000}) { wav(dir / "edge.wav",rate,2,16,rate); check(callnames::ReadAudio(dir / "edge.wav", samples,error), "stereo rate boundaries"); }
        wav(dir / "five.wav",8000,1,16,40000); check(callnames::ReadAudio(dir / "five.wav", samples,error), "five seconds accepted");
        wav(dir / "long.wav",8000,1,16,40001); check(!callnames::ReadAudio(dir / "long.wav",samples,error), "over five seconds rejected");
        wav(dir / "eight.wav",8000,1,8,100); check(rejects([&]{ ImportAudio(mod,dir / "eight.wav"); }), "8-bit WAV rejected before copying");
        wav(dir / "rate.wav",7999,1,16,100); check(!callnames::ReadAudio(dir / "rate.wav",samples,error), "bad rate rejected");
        std::ofstream(dir / "bad.mp3") << "broken"; check(!callnames::ReadAudio(dir / "bad.mp3",samples,error), "invalid MP3 rejected");
        check(!SafeRelative(mod / "callnames", "../outside.wav") && !SafeRelative(mod / "callnames", "C:/outside.wav"), "path traversal rejected");
        check(ImportAudio(mod, dir / "sine.wav") != normal, "imports never overwrite audio");
        check(rejects([&]{ CreateMod(dir,"../escape"); }), "invalid mod rejected");
        const auto config = dir / "Config.toml";
        std::ofstream(config) << "# preserved\n[mods] # section comment\nenabled = [ # array comment\n  'Existing', # do not enable 'comment'\n  \"A # B\",\n]\n[video]\nshow_fps = true\n";
        check(tstudio::ReadEnabledMods(config) == std::vector<std::string>{"Existing", "A # B"}, "multiline enabled mods with comments");
        check(tstudio::SetModEnabled(config, "New", true), "enable mod in multiline config");
        { std::ifstream input(config,std::ios::binary); auto parsed = toml::parse(input); check(toml::find<std::vector<std::string>>(parsed,"mods","enabled") == std::vector<std::string>{"Existing","A # B","New"} && toml::find<bool>(parsed,"video","show_fps"), "valid config retains existing mods and settings"); }
        std::ofstream(config) << "[mods]\nenabled = [\n  'Existing'\n";
        check(!tstudio::SetModEnabled(config,"New",true), "malformed enabled array is not overwritten");
        std::ofstream(config,std::ios::binary) << "# top\r\n[paths]\r\ndvd_root = 'D:\\x' # keep\r\n";
        check(tstudio::WriteSetting(config,"ui","language","\"en\"") && tstudio::WriteSetting(config,"ui","language","\"es\""), "shared helper language updates");
        { std::ifstream input(config,std::ios::binary); const std::string text((std::istreambuf_iterator<char>(input)),{}); check(text == "# top\r\n[paths]\r\ndvd_root = 'D:\\x' # keep\r\n\r\n[ui]\r\nlanguage = \"es\"\r\n", "other studios' exact config formatting retained"); }
        std::ofstream(config.string() + ".tmp") << "preserve";
        check(!tstudio::WriteSetting(config,"ui","language","\"en\"") && tstudio::ReadSetting(config,"ui","language").value_or("") == "es", "shared config writer preserves destination on temporary collision");
        const auto temp = mod / "callnames/callnames.toml.studio.tmp";
        std::ofstream(temp) << "do not truncate";
        check(rejects([&]{ Save(mod,round); }) && fs::file_size(temp) == 15, "exclusive temporary creation preserves existing files");
        fs::remove(temp);
        const auto outside = dir / "outside.txt"; std::ofstream(outside) << "preserve";
        std::error_code linkError; fs::create_symlink(outside,temp,linkError);
        if (!linkError) { check(rejects([&]{ Save(mod,round); }) && fs::file_size(outside) == 8, "temporary symlink cannot redirect writes outside mod"); fs::remove(temp); }
        else std::cout << "SKIP symlink creation unavailable: " << linkError.message() << '\n';
        round["callname"] = Value::array_type{}; Save(mod,round); check(Load(mod).at("callname").as_array().empty(), "delete last entry round trip");
        fs::create_directories(dir / "Bad/callnames"); std::ofstream(dir / "Bad/callnames/callnames.toml") << "broken = [";
        check(rejects([&]{ UsedVoices(dir); }), "unreadable manifest blocks allocation");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    fs::remove_all(dir);
    return failures ? 1 : 0;
}
}
