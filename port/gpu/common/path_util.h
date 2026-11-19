// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: where the GPU library keeps its files: BB_GPU_USER_DIR (run.sh points it at the
// port's user folder), else ./user. A folder is created the first time it is asked for.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Common::FS {

enum class PathType {
    UserDir,
    LogDir,
    ScreenshotsDir,
    ShaderDir,
    TempDataDir,
    GameDataDir,
    SysModuleDir,
    DownloadDir,
    CapturesDir,
    CheatsDir,
    PatchesDir,
    MetaDataDir,
    CustomTrophy,
    CustomConfigs,
    CacheDir,
    FontsDir,
    TrophyDir,
    HomeDir,
    CustomModulesDir,
};

[[nodiscard]] std::string PathToUTF8String(const std::filesystem::path& path);

/// The folder of `type` (created now if it is not there).
[[nodiscard]] const std::filesystem::path& GetUserPath(PathType type);
[[nodiscard]] std::string GetUserPathString(PathType type);

} // namespace Common::FS
