#pragma once
#include "../prelude.cuh"
#include "runtime_options.cuh"
#include "../contracts/device_config.cuh"

namespace optiland {
namespace {
struct ConfigureAspherePolicyInputs {
    DeviceConfig& cfg;
    const bool& production_asphere_policy;
    const bool& unified_certified_policy;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
template <class Inputs>
inline native_detail::PhaseOutcome configure_asphere_policy(Inputs& inputs) {
    auto& cfg = inputs.cfg;
    auto& production_asphere_policy = inputs.production_asphere_policy;
    auto& unified_certified_policy = inputs.unified_certified_policy;
    if (env_flag_enabled("OPTILAND_DE_CUDA_ASPHERE_STRICT_FIRST_ROOT")) {
        cfg.asphere_initial_root_mode = 0;
    } else if (env_flag_enabled("OPTILAND_DE_CUDA_ASPHERE_TRUST_INITIAL_ROOT")) {
        cfg.asphere_initial_root_mode = 1;
    } else if (env_flag_enabled(
                   "OPTILAND_DE_CUDA_ASPHERE_CONFIRM_INITIAL_ROOT_NO_FALLBACK")) {
        cfg.asphere_initial_root_mode = 3;
    } else if (production_asphere_policy) {
        cfg.asphere_initial_root_mode = 5;
        cfg.asphere_confirmation_segments = 6;
        cfg.asphere_slope_root_domain_enabled = 1;
        cfg.asphere_slope_root_domain_min_tangent_deg =
            kAsphereRootDomainMinimumTangentAxisAngleDeg;
        cfg.asphere_aiming_local_branch = 1;
        cfg.asphere_auxiliary_local_branch = 1;
        cfg.asphere_slope_root_domain_reuse = 1;
    } else if (unified_certified_policy) {
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
        // The release-specialized binary contains exactly the retained UC10
        // certified route.  Mode selection remains a host-side policy choice,
        // but experimental device variants are not materialized into this
        // build's hot call graph.
        cfg.asphere_initial_root_mode = 23;
#else
        const bool uc5_requested =
            env_flag_enabled("OPTILAND_ASPHERE_UC5") ||
            env_flag_enabled("OPTILAND_ASPHERE_UC5_SYMMETRY") ||
            env_flag_enabled("OPTILAND_ASPHERE_UC5_PRIMARY_EARLY_ABORT") ||
            env_flag_enabled("OPTILAND_ASPHERE_UC5_CANDIDATE_BATCH");
        const bool uc7_residual_horner =
            env_flag_enabled("OPTILAND_ASPHERE_UC7_RESIDUAL_HORNER");
        const bool uc7_fused_horner =
            env_flag_enabled("OPTILAND_ASPHERE_UC7_FUSED_HORNER");
        const bool uc8_diagnostic =
            env_flag_enabled("OPTILAND_ASPHERE_UC8_DIAGNOSTIC");
        const bool uc8_abort_clamp4 =
            env_flag_enabled("OPTILAND_ASPHERE_UC8_ABORT_CLAMP4");
        const bool uc8_abort_stall8 =
            env_flag_enabled("OPTILAND_ASPHERE_UC8_ABORT_STALL8");
        const bool uc9_interval_baseline =
            env_flag_enabled("OPTILAND_ASPHERE_UC9_INTERVAL_BASELINE");
        const bool uc9_interval_fused_inline =
            env_flag_enabled("OPTILAND_ASPHERE_UC9_INTERVAL_FUSED_INLINE");
        const bool uc9_interval_fused_noinline =
            env_flag_enabled("OPTILAND_ASPHERE_UC9_INTERVAL_FUSED_NOINLINE");
        const bool uc10_qprime_baseline =
            env_flag_enabled("OPTILAND_ASPHERE_UC10_QPRIME_BASELINE");
        const bool uc10_qprime_reuse =
            env_flag_enabled("OPTILAND_ASPHERE_UC10_QPRIME_REUSE");
        const bool uc12_affine_baseline =
            env_flag_enabled("OPTILAND_ASPHERE_UC12_AFFINE_BASELINE");
        const bool uc12_affine_reuse =
            env_flag_enabled("OPTILAND_ASPHERE_UC12_AFFINE_REUSE");
        const bool uc14_baseline =
            env_flag_enabled("OPTILAND_ASPHERE_UC14_BASELINE");
        const bool uc14_raw_prune =
            env_flag_enabled("OPTILAND_ASPHERE_UC14_RAW_PRUNE");
        const bool uc14_defer_hi =
            env_flag_enabled("OPTILAND_ASPHERE_UC14_DEFER_HI");
        const bool uc14_known_hi =
            env_flag_enabled("OPTILAND_ASPHERE_UC14_KNOWN_HI");
        const bool uc14_position_certificate =
            env_flag_enabled("OPTILAND_ASPHERE_UC14_POSITION_CERT");
        const bool uc14_combined =
            env_flag_enabled("OPTILAND_ASPHERE_UC14_COMBINED");
        const bool uc14_requested =
            uc14_baseline || uc14_raw_prune || uc14_defer_hi ||
            uc14_known_hi || uc14_position_certificate || uc14_combined;
        cfg.asphere_initial_root_mode =
            uc14_requested
                ? (uc14_combined
                       ? 31
                       : (uc14_position_certificate
                              ? 30
                              : (uc14_known_hi
                                     ? 29
                                     : (uc14_defer_hi
                                            ? 28
                                            : (uc14_raw_prune ? 27 : 26)))))
                : (uc12_affine_baseline || uc12_affine_reuse)
                ? (uc12_affine_reuse ? 25 : 24)
                : (uc10_qprime_baseline || uc10_qprime_reuse)
                ? (uc10_qprime_reuse ? 23 : 22)
                : (uc9_interval_baseline || uc9_interval_fused_inline ||
             uc9_interval_fused_noinline)
                ? (uc9_interval_fused_inline
                       ? 20
                       : (uc9_interval_fused_noinline ? 21 : 19))
                : (uc8_diagnostic || uc8_abort_clamp4 || uc8_abort_stall8)
                ? (uc8_abort_clamp4 && uc8_abort_stall8
                       ? 18
                       : (uc8_abort_clamp4
                              ? 16
                              : (uc8_abort_stall8 ? 17 : 15)))
                : (uc7_residual_horner || uc7_fused_horner)
                ? (uc7_residual_horner && uc7_fused_horner
                       ? 14
                       : (uc7_residual_horner ? 12 : 13))
                : env_flag_enabled("OPTILAND_ASPHERE_UC6_PREFIX_EVAL_REUSE")
                ? 11
                : (env_flag_enabled(
                       "OPTILAND_ASPHERE_UC4_INITIAL_MODEL_FASTPATH") ||
                   uc5_requested)
                ? 10
                : (env_flag_enabled("OPTILAND_ASPHERE_UC3_FAILURE_CERT")
                       ? 9
                       : (env_flag_enabled("OPTILAND_ASPHERE_UC2_EXACT_S1_CERT")
                              ? 8
                              : (env_flag_enabled("OPTILAND_ASPHERE_UC1_PREFIX_CERT")
                                     ? 7
                                      : 6)));
#endif
        cfg.asphere_confirmation_segments = 6;
        cfg.asphere_slope_root_domain_enabled = 1;
        cfg.asphere_unified_contract_domain_enabled = 1;
        cfg.asphere_slope_root_domain_min_tangent_deg =
            kAsphereRootDomainMinimumTangentAxisAngleDeg;
        cfg.asphere_slope_root_domain_reuse = 1;
        cfg.asphere_aiming_local_branch = 0;
        cfg.asphere_auxiliary_local_branch = 0;
    } else if (asphere_root_policy() == AsphereRootPolicy::FastRecover) {
        cfg.asphere_initial_root_mode = 1;
    } else if (asphere_root_policy() == AsphereRootPolicy::CertifiedFirstRoot) {
        cfg.asphere_initial_root_mode = 0;
    } else {
        cfg.asphere_initial_root_mode = 2;
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace
} // namespace optiland
