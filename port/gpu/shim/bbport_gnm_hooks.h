// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: BB_GNM_WRITES_TRACE=1 keeps the packets our GnmDriver writes into command buffers the
// game reserved, so a malformed submission can name the driver functions that wrote over it.
#pragma once

#include <cstdint>
#include <source_location>

namespace BbGnmHooks {
/// Prints the recent driver writes overlapping [begin, begin + bytes) (BB_GNM_WRITES_TRACE).
void ReportDriverWrites(std::uintptr_t begin, std::uint64_t bytes);

/// Packets our GnmDriver writes into space the game reserved ([commands, commands + dwords)),
/// recorded when the guard goes out of scope.
struct DriverWrite {
    std::uint32_t* commands;
    std::uint64_t dwords;
    const char* writer;
    DriverWrite(std::uint32_t* commands_, std::uint64_t dwords_,
                std::source_location where = std::source_location::current())
        : commands(commands_), dwords(dwords_), writer(where.function_name()) {}
    ~DriverWrite();
};
} // namespace BbGnmHooks
