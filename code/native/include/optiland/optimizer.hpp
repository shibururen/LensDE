#pragma once

#include "optiland/core.hpp"

#include <functional>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "optiland/optimizer/session_fwd.hpp"
#include "optiland/optimizer/scaling.hpp"
#include "optiland/optimizer/de_strategies.hpp"
#include "optiland/optimizer/generation.hpp"
#include "optiland/optimizer/variable_transform.hpp"
#include "optiland/optimizer/de_options.hpp"
#include "optiland/optimizer/cuda_evaluation.hpp"
#include "optiland/optimizer/timing.hpp"
#include "optiland/optimizer/fitness.hpp"
#include "optiland/optimizer/basins.hpp"
#include "optiland/optimizer/micro_islands.hpp"
#include "optiland/optimizer/results.hpp"
#include "optiland/optimizer/dls_options.hpp"
#include "optiland/optimizer/trace.hpp"
#include "optiland/optimizer/hybrid_options.hpp"
#include "optiland/optimizer/adaptive_options.hpp"
#include "optiland/optimizer/progress.hpp"
#include "optiland/optimizer/engine.hpp"
#include "optiland/optimizer/cuda_api.hpp"
