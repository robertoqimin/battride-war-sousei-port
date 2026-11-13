// SPDX-FileCopyrightText: Copyright 2026 kr-port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// kr-port: the guest hooks were bbport's Bloodborne code (games/bloodborne/, not part of this
// port). This game installs none: what the core asks of them answers as for any game without
// hooks (no camera state, no native mouse look: the mouse drives the right stick).
#include "shim/bbport_guest_hooks.h"

namespace BbGuestHooks {
void SetDisplayAspect(float) {}
std::uint64_t CameraState() {
    return 0;
}
bool FreeCameraActive() {
    return false;
}
void ApplyGameEffects() {}
bool CameraLookAvailable() {
    return false;
}
bool CameraLookAdd(float, float) {
    return false;
}
} // namespace BbGuestHooks
