// SPDX-License-Identifier: GPL-3.0-or-later
// The editable project of a uniform, saved next to its uniform.ppu so it can be opened again with every
// layer: UserData/arrange/uniforms/<name>/studio/project.txt + one PNG per image layer (+ thumb.png).
// Only the player's own work goes there (their layers and choices), never game data.
#pragma once
#include "layers.h"
#include "pieces.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace ustudio {

struct Project {
    std::string name;
    std::array<Canvas, kKindCount> canvases;
    Pieces pieces;
    bool piecesOn = false;
    std::array<int, 10> backup{};
};

bool SaveProject(const std::filesystem::path& dir, const Project& p, std::string* error);
bool LoadProject(const std::filesystem::path& dir, Project* p, std::string* error);

// Saved uniforms, sorted by name: those with a project (studio/project.txt) and, as `legacy`, those with
// only a uniform.ppu (saved before projects existed: they open with each atlas as one base image).
struct SavedUniform {
    std::string name;
    std::filesystem::path studio;
    tstudio::Image thumb;  // studio/thumb.png (the figure in the uniform), empty if missing
    bool legacy = false;
    std::filesystem::path ppu;
};
std::vector<SavedUniform> ListProjects(const std::filesystem::path& uniformsRoot);

}  // namespace ustudio
