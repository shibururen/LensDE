#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

std::vector<double> weighted_rms_residuals_from_score(const Score& score, double fail_merit);

std::vector<double> adaptive_quality_residuals_from_score(
    const Score& score,
    const MeritConfig& config);

std::vector<double> unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config);

double signed_sqrt_penalty_residual(const MeritContribution& contribution);

double signed_normalized_operand_residual(
    const MeritContribution& contribution,
    const MeritConfig& config);

double guard_band_residual(
    const MeritContribution& contribution,
    const MeritConfig& config);

void append_guard_band_residuals(
    const Score& score,
    const MeritConfig& config,
    std::vector<double>& residuals);

bool is_single_ray_spot_guard_contribution(const MeritContribution& contribution);

bool is_single_ray_wavefront_guard_contribution(const MeritContribution& contribution);

bool single_ray_guard_residual_replaces_contribution(
    const Score& score,
    const MeritContribution& contribution);

std::size_t single_ray_guard_residual_count(const Score& score);

void append_single_ray_guard_residuals(
    const Score& score,
    std::vector<double>& residuals,
    double scale = 1.0);

void append_ray_spot_residuals(
    const Score& score,
    std::vector<double>& residuals,
    double scale = 1.0);

std::vector<double> expanded_unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config,
    bool include_guard_band = false);

std::vector<double> normalized_unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config,
    bool include_guard_band = false);

std::vector<double> ray_spot_unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config);

std::vector<double> hybrid_unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config,
    bool include_guard_band = false);

std::vector<double> expanded_ray_spot_unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config,
    bool include_normalized = false,
    double ray_scale = 1.0);

bool expanded_residual_mode(const std::string& mode);

bool normalized_operand_residual_mode(const std::string& mode);

bool guarded_expanded_residual_mode(const std::string& mode);

bool guarded_normalized_residual_mode(const std::string& mode);

bool hybrid_residual_mode(const std::string& mode);

bool guarded_hybrid_residual_mode(const std::string& mode);

bool ray_spot_residual_mode(const std::string& mode);

bool expanded_ray_spot_residual_mode(const std::string& mode);

bool expanded_normalized_ray_spot_residual_mode(const std::string& mode);

bool light_expanded_ray_spot_residual_mode(const std::string& mode);

bool light_expanded_normalized_ray_spot_residual_mode(const std::string& mode);

std::vector<double> residuals_from_score(
    const Score& score,
    const MeritConfig& config,
    const std::string& residual_mode = "expanded");

} }  // namespace optiland::optimizer_detail
