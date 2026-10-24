// SPDX-License-Identifier: GPL-3.0-or-later
// AI upscaling with Real-ESRGAN ncnn-vulkan (MIT; Vulkan, so AMD, NVIDIA and Intel), downloaded into the
// player's UserData on first use. Its own models realesrgan-x4plus-anime and realesrgan-x4plus (BSD) come
// with it; 4x-AnimeSharp (Kim2091, CC BY-NC-SA 4.0) is downloaded from its author's Hugging Face page.
// Any other NCNN model (<name>.param + <name>.bin) put in the models folder is offered too. Nothing of it
// ships with the game.
#pragma once
#include "transfer.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tstudio {
struct UpscaleModel {
    std::string id;     // the engine's -n name
    std::string label;  // shown in the list
    bool ready = false; // engine and model files present
};
// AnimeSharp first (the default), then the engine's two, then the player's own.
std::vector<UpscaleModel> UpscaleModels(const std::filesystem::path& tools);
// What has to be downloaded for this model (engine and/or model), as text for the consent popup; empty: nothing.
std::string UpscaleDownloads(const std::filesystem::path& tools, const std::string& model);
bool DownloadUpscaler(const std::filesystem::path& tools, const std::string& model, std::string* error);
// Unpacks a .zip into `dir` with the system's own tools (tar on Windows, unzip elsewhere).
bool UnpackZip(const std::filesystem::path& zip, const std::filesystem::path& dir, std::string* error);
// Every PNG under `in` (recursive) upscaled into `out` under the same name; scale 2 or 4 (a 4x model, halved
// for 2); the longer side never above 4096 (a smaller power-of-two factor then). progress counts images.
bool Upscale(const std::filesystem::path& tools, const std::string& model, int scale, const std::filesystem::path& in,
             const std::filesystem::path& out, Progress* progress, std::string* error);
} // namespace tstudio
