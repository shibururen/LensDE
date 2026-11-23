#pragma once
#include "optiland/core.hpp"
#include <functional>
#include <vector>

namespace optiland {
using ProgressCallback = std::function<void(
    int generation,
    const Score& best,
    const std::vector<double>& best_values)>;



}  // namespace optiland
