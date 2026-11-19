// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shim/bb_env.h"
#include "bbport_gnm_hooks.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <utility>
#include <vector>
#include <unistd.h>

namespace BbGnmHooks {
namespace {
using u64 = std::uint64_t;

struct DriverRecord {
    u64 begin, bytes;
    const char* writer;
    int thread = 0;
    u64 when = 0;
};
constexpr u64 RecordCount = 65536;
std::mutex driver_records_mutex;
DriverRecord driver_records[RecordCount]{};
u64 driver_record_next = 0;

bool TraceDriverWrites() {
    static const bool on = bb_env_on("BB_GNM_WRITES_TRACE");
    return on;
}
} // namespace

void ReportDriverWrites(std::uintptr_t begin, std::uint64_t bytes) {
    if (!TraceDriverWrites()) return;
    std::scoped_lock lock{driver_records_mutex};
    std::vector<std::pair<u64, DriverRecord>> matching;
    const u64 first = driver_record_next > RecordCount ? driver_record_next - RecordCount : 0;
    for (u64 i = driver_record_next; i > first; --i) {
        const auto& record = driver_records[(i - 1) % RecordCount];
        if (record.begin < begin + bytes && record.begin + record.bytes > begin) {
            matching.emplace_back(i - 1, record);
        }
    }
    std::sort(matching.begin(), matching.end(), [](const auto& a, const auto& b) {
        return a.second.when > b.second.when;
    });
    unsigned shown = 0;
    for (const auto& [number, record] : matching) {
        if (shown == 24) break;
        std::fprintf(stderr, "  Gnm driver write #%llu at %#llx +%llu bytes: %s (thread %d)\n",
                     (unsigned long long)number, (unsigned long long)record.begin,
                     (unsigned long long)record.bytes, record.writer, record.thread);
        ++shown;
    }
    if (!shown) std::fprintf(stderr, "  no recent Gnm driver writes overlap this window\n");
}

DriverWrite::~DriverWrite() {
    if (!commands || !TraceDriverWrites()) {
        return;
    }
    static thread_local const int thread = gettid();
    std::scoped_lock lock{driver_records_mutex};
    driver_records[driver_record_next++ % RecordCount] = {
        reinterpret_cast<u64>(commands), dwords * 4, writer, thread,
        u64(std::chrono::steady_clock::now().time_since_epoch().count())};
}
} // namespace BbGnmHooks
