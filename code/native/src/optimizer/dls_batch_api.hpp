#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

std::vector<PolishWorkResult> run_batched_micro_dls_scaled(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const MeritConfig& merit_config,
    const std::vector<MicroDlsInput>& inputs,
    const PolishOptions& options,
    InternalScalingMode internal_mode,
    bool rollback,
    const ScoreBatchEvaluator& evaluate_scores);

} }  // namespace optiland::optimizer_detail
