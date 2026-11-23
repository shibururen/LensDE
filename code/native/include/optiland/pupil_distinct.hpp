#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <set>
#include <vector>

namespace optiland {
namespace detail {

// Certify that no two pupil positions can match the identity transform.
// This is deliberately independent of weights: an uncertain case uses the
// original representative search, preserving its first-match semantics.
template <class Pupils>
bool pupil_positions_are_distinct(const Pupils& pupils, double tolerance) {
    if (!std::isfinite(tolerance) || tolerance < 0.0) {
        return false;
    }
    for (const auto& pupil : pupils) {
        if (!std::isfinite(pupil.px) || !std::isfinite(pupil.py)) {
            return false;
        }
    }
    std::vector<std::size_t> order(pupils.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return pupils[a].px < pupils[b].px;
    });
    std::multiset<double> active_y;
    std::size_t left = 0;
    for (std::size_t right = 0; right < order.size(); ++right) {
        const auto& pupil = pupils[order[right]];
        while (left < right &&
               pupil.px - pupils[order[left]].px > tolerance) {
            active_y.erase(active_y.find(pupils[order[left]].py));
            ++left;
        }
        // Test the nearest y on either side, without quantizing coordinates
        // or subtracting a tolerance from a search key at floating boundaries.
        const auto next = active_y.lower_bound(pupil.py);
        if (next != active_y.end() && std::abs(*next - pupil.py) <= tolerance) {
            return false;
        }
        if (next != active_y.begin() &&
            std::abs(*std::prev(next) - pupil.py) <= tolerance) {
            return false;
        }
        active_y.insert(pupil.py);
    }
    return true;
}

} // namespace detail
} // namespace optiland
