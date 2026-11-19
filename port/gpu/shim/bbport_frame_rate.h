// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: a frame rate limit set while the game runs (the overlay's FPS row, the 30 FPS hotkey).
// The video out paces presented frames to it (platform/videoout/display.cpp).
#pragma once

#include <atomic>

namespace BbFrameRate {
inline std::atomic<int> live_limit{0};
/// A frame rate limit while the game runs (0: the launched mode and limit).
inline void SetLimit(int fps) {
    live_limit.store(fps, std::memory_order_relaxed);
}
inline int Limit() {
    return live_limit.load(std::memory_order_relaxed);
}
} // namespace BbFrameRate
