#pragma once
#include "detail.hpp"

namespace optiland { namespace raytrace_detail {

inline RayAimingDiagnosticsState& diagnostics_state() {
    static RayAimingDiagnosticsState state;
    return state;
}

inline bool diagnostics_active() {
    return diagnostics_state().enabled.load(std::memory_order_relaxed);
}

inline void add_diag(std::atomic<long long>& counter, long long value ) {
    if (diagnostics_active()) {
        counter.fetch_add(value, std::memory_order_relaxed);
    }
}

inline long long scaled_residual(double residual) {
    if (!std::isfinite(residual) || residual < 0.0) {
        return -1;
    }
    const double scaled = residual * kDiagResidualScale;
    if (!std::isfinite(scaled) || scaled > static_cast<double>(std::numeric_limits<long long>::max())) {
        return std::numeric_limits<long long>::max();
    }
    return static_cast<long long>(std::llround(scaled));
}

inline void update_diag_max(std::atomic<long long>& target, long long value) {
    long long current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(
               current,
               value,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

inline void add_diag_residual(std::atomic<long long>& count,
                       std::atomic<long long>& sum_scaled,
                       std::atomic<long long>& max_scaled,
                       double residual) {
    if (!diagnostics_active()) {
        return;
    }
    const long long scaled = scaled_residual(residual);
    if (scaled < 0) {
        return;
    }
    count.fetch_add(1, std::memory_order_relaxed);
    sum_scaled.fetch_add(scaled, std::memory_order_relaxed);
    update_diag_max(max_scaled, scaled);
}

inline long long aiming_clock_ns() {
#ifdef _WIN32
    // MinGW 8's steady_clock can have millisecond granularity on Windows;
    // model/chief solves are often much shorter than one tick.
    static const double ns_per_tick=[] {
        LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
        return 1.0e9 / static_cast<double>(frequency.QuadPart);
    }();
    LARGE_INTEGER count; QueryPerformanceCounter(&count);
    return static_cast<long long>(count.QuadPart * ns_per_tick);
#else
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

} } // namespace optiland::raytrace_detail
