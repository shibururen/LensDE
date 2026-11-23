#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

std::string normalized_token(std::string text);

bool distortion_model_allows_cuda(const MeritConfig& config);

bool de_cuda_required();

bool polish_uses_batch_merit(const PolishOptions& options);

bool polish_uses_trf(const PolishOptions& options);

bool adaptive_quality_enabled(const MeritConfig& config);

bool unified_trace_gate_enabled(const MeritConfig& config);

bool is_spot_contribution(const MeritContribution& contribution);

bool is_wavefront_contribution(const MeritContribution& contribution);

bool is_image_quality_contribution(const MeritContribution& contribution);

bool is_spot_operand_token(const std::string& name);

bool is_wavefront_operand_token(const std::string& name);

bool merit_prefers_wavefront_objective(const MeritConfig& config);

} }  // namespace optiland::optimizer_detail
