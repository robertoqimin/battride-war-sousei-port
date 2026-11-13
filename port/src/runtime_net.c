// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* kr-port: bbport's online module (gpu/bbnet20, Bloodborne's online play) is not part of this
 * port; this game plays offline (runtime_kroffline.c answers its network and PSN imports). The
 * entry points stay so the rest of the runtime is unchanged. */
#include "runtime.h"

void runtime_net_configure(const char *app0, const char *serial, const char *title, const char *version) {
    (void)app0; (void)serial; (void)title; (void)version;
}
int runtime_net_enabled(void) { return 0; }
int runtime_net_symbol(const char *symbol) { (void)symbol; return 0; }
uintptr_t runtime_net_resolve(const char *name) { (void)name; return 0; }
void runtime_net_image_ready(uintptr_t base, uint64_t size) { (void)base; (void)size; }
void runtime_net_image_mapped(uint8_t *image, uint64_t size) { (void)image; (void)size; }
void runtime_net_start(void) {}
