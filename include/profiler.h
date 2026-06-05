#pragma once

#ifndef DEBUG_PROFILING
#define DEBUG_PROFILING 0
#endif
#if DEBUG_PROFILING

#include <cstdio>
#include <chrono>

struct ScopedTimer {
    const char* name;
    std::chrono::steady_clock::time_point start;

    ScopedTimer(const char* name) : name(name), start(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        fprintf(stderr, "[PROFILE] %s: %ld ns (%.3f ms)\n", name, ns, ns / 1000000.0);
    }
};

#else

struct ScopedTimer {
    ScopedTimer(const char*) {}
};

#endif
