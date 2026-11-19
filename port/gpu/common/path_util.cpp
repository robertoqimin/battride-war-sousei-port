// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include "shim/bb_env.h"
#include <cstdlib>
#include <mutex>

#include <limits.h>

#include "common/logging/log.h"
#include "common/path_util.h"

namespace Common::FS {
namespace fs = std::filesystem;
namespace {

constexpr std::size_t NumTypes = std::size_t(PathType::CustomModulesDir) + 1;

/// Sub-folder of the user folder for each type ("" = the user folder itself).
constexpr std::array<const char*, NumTypes> SubFolders = {
    "",          "log",       "screenshots",   "shader",         "temp",
    "data",      "sys_modules", "download",    "captures",       "cheats",
    "patches",   "game_data", "custom_trophy", "custom_configs", "cache",
    "fonts",     "trophy",    "home",          "custom_modules",
};

struct Paths {
    std::mutex mutex;
    std::array<fs::path, NumTypes> paths;
    std::array<bool, NumTypes> ready{};

    Paths() {
        const char* env = bb_env("BB_GPU_USER_DIR");
        const fs::path user = env && *env ? fs::path{env} : fs::current_path() / "user";
        for (std::size_t i = 0; i < NumTypes; ++i) {
            paths[i] = *SubFolders[i] ? user / SubFolders[i] : user;
        }
    }
};

Paths& Instance() {
    static Paths paths;
    return paths;
}

} // namespace

std::string PathToUTF8String(const fs::path& path) {
    const auto text = path.u8string();
    return std::string{text.begin(), text.end()};
}

const fs::path& GetUserPath(PathType type) {
    auto& state = Instance();
    const auto index = std::size_t(type);
    std::scoped_lock lock{state.mutex};
    if (!state.ready[index]) {
        std::error_code ec;
        fs::create_directories(state.paths[index], ec);
        state.ready[index] = true;
    }
    return state.paths[index];
}

std::string GetUserPathString(PathType type) {
    return PathToUTF8String(GetUserPath(type));
}

} // namespace Common::FS
