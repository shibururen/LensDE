#pragma once
#include "optiland/optimizer/de_options.hpp"
#include "optiland/optimizer/dls_options.hpp"
#include "optiland/optimizer/generation.hpp"
#include "optiland/optimizer/trace.hpp"
#include "optiland/optimizer/scaling.hpp"

namespace optiland {
struct HybridDeDlsOptions {
    DifferentialEvolutionOptions de;
    PolishOptions polish;
    int outer_epochs = 6;
    int parent_count = 4;
    double diversity = 0.03;
    double refresh_fraction = 0.10;
    bool rollback = true;
    bool inject_best = true;
    bool polish_enabled = true;
    bool parallel_polish = true;
    bool batched_polish = false;
    int polish_workers = 0;
    DistanceMetric distance_metric = DistanceMetric::BoundNormalized;
    InternalScalingMode internal_scaling = InternalScalingMode::LegacyOptical;
    PopulationGenerationOptions refresh_generation;
    HybridTraceCallback trace;
};


}  // namespace optiland
