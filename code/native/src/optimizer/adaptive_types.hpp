#pragma once
#include "support.hpp"

namespace optiland { namespace optimizer_detail {

struct DeviceCertificateReset {
                    ~DeviceCertificateReset() { evaluation_set_certificate_context(0,{},0.0); }
                };

struct CachedTopologySimilarity {
        double similarity = 0.0;
    };

struct CachedParsedTopologyKey {
        ParsedStructuralTopologyKey parsed;
        std::uint32_t id = 0;
    };

struct BasinConsiderResult {
        std::string action = "skipped";
        int basin_id = -1;
        int nearest_basin_id = -1;
        double nearest_distance = std::numeric_limits<double>::quiet_NaN();
        std::string previous_source;
    };

struct ActiveBasinLifecycleState {
        double best_merit = std::numeric_limits<double>::infinity();
        int stale_epochs = 0;
        int cooldown_until_epoch = 0;
        int last_seen_epoch = 0;
    };

struct ActiveFocusLifecycleState {
        double baseline_merit = std::numeric_limits<double>::quiet_NaN();
        double best_merit = std::numeric_limits<double>::quiet_NaN();
        double last_rel_improvement = std::numeric_limits<double>::quiet_NaN();
        int ready_epoch = 0;
        int cooldown_until_epoch = 0;
        int last_seen_epoch = 0;
        bool ready = false;
    };

struct ActiveFocusRotationEntry {
        std::string key;
        int last_epoch = 0;
    };

struct TraceableRefreshArchiveEntry {
        std::vector<double> scaled_values;
        CandidateFitness fitness;
    };

struct AdaptiveGlassToleranceState {
        bool enabled = false;
        bool updated = false;
        bool trigger_ready = true;
        bool bridge_active = false;
        bool focus_frozen = false;
        bool stop_when_ready = false;
        std::string trigger_mode = "warmup";
        int focus_clear_count = 0;
        int focus_clear_target = 0;
        int sample_count = 0;
        int elite_count = 0;
        double previous = std::numeric_limits<double>::quiet_NaN();
        double current = std::numeric_limits<double>::quiet_NaN();
        double target = std::numeric_limits<double>::quiet_NaN();
        double metric = std::numeric_limits<double>::quiet_NaN();
    };

struct FamilyRepresentative {
            std::string key;
            std::vector<double> scaled;
        };

struct ActivePlateauEvictCandidate {
                int index = -1;
                double nearest_anchor_distance =
                    std::numeric_limits<double>::infinity();
            };

struct ActivePlateauReplacement {
                std::vector<double> scaled;
                CandidateFitness fitness;
                std::string source;
            };

struct ActivePlateauCandidateSnapshot {
            double merit = std::numeric_limits<double>::quiet_NaN();
            std::vector<double> scaled;
        };

struct MeritStats {
        int count = 0;
        double mean = std::numeric_limits<double>::quiet_NaN();
        double median = std::numeric_limits<double>::quiet_NaN();
        double worst = std::numeric_limits<double>::quiet_NaN();
    };

struct EarlyStopPoint {
        int epoch = 0;
        int topology_count = 0;
        double criterion = std::numeric_limits<double>::quiet_NaN();
    };

struct PBestGuideStats {
        int self = 0;
        int same_key = 0;
        int basin = 0;
        int global = 0;
        int none = 0;
        int key_match = 0;
        int key_mismatch = 0;
        int key_unknown = 0;
        int active_plateau_suppressed = 0;
        int active_plateau_global_rescue = 0;
        int geometry_memory_checked = 0;
        int geometry_memory_hit = 0;
        int geometry_memory_redirect = 0;
        int geometry_memory_rescue = 0;
        double geometry_memory_distance_seconds = 0.0;
        int geometry_memory_difference_checked = 0;
        int geometry_memory_difference_hit = 0;
        int geometry_memory_difference_applied = 0;
        int geometry_memory_difference_same_key = 0;
        int geometry_memory_difference_geometry_trigger = 0;
        int geometry_memory_difference_key_trigger = 0;
        int geometry_memory_difference_exact_key_trigger = 0;
        int geometry_memory_difference_family_key_trigger = 0;
        int geometry_memory_difference_outward_flip = 0;
        int geometry_memory_difference_capped = 0;
        double geometry_memory_difference_seconds = 0.0;
        int geometry_memory_qohr_checked = 0;
        int geometry_memory_qohr_applied = 0;
        int geometry_memory_qohr_degenerate = 0;
        int geometry_memory_qohr_quality_gated = 0;
        int geometry_memory_qohr_family_weighted = 0;
        double geometry_memory_qohr_quality_weight_sum = 0.0;
        double geometry_memory_qohr_rotation_sum = 0.0;
        double geometry_memory_qohr_quality_projection_before_sum = 0.0;
        double geometry_memory_qohr_quality_projection_after_sum = 0.0;
        double geometry_memory_qohr_history_projection_before_sum = 0.0;
        double geometry_memory_qohr_history_projection_after_sum = 0.0;
        double geometry_memory_qohr_step_norm_ratio_sum = 0.0;
        double geometry_memory_qohr_seconds = 0.0;
        int geometry_memory_exact_escape_resident_generation = 0;
        int geometry_memory_exact_escape_activation = 0;
        int geometry_memory_exact_escape_active_generation = 0;
        int geometry_memory_exact_escape_success = 0;
        int geometry_memory_exact_escape_timeout = 0;
        int geometry_memory_exact_escape_crossing_trial = 0;
        int geometry_memory_exact_escape_crossing_accepted = 0;
        int geometry_memory_exact_escape_novelty_eligible = 0;
        int geometry_memory_exact_escape_novelty_accepted = 0;
        int geometry_memory_family_escape_activation = 0;
        int geometry_memory_family_escape_active_generation = 0;
        int geometry_memory_family_escape_success = 0;
        int geometry_memory_family_escape_crossing_trial = 0;
        int geometry_memory_family_escape_crossing_accepted = 0;
        int geometry_memory_family_escape_novelty_eligible = 0;
        int geometry_memory_family_escape_novelty_accepted = 0;
        int geometry_memory_novelty_guide_generation = 0;
        int geometry_memory_novelty_guide_unseen_generation = 0;
        int geometry_memory_novelty_pbest_checked = 0;
        int geometry_memory_novelty_pbest_redirect = 0;
    };

struct DeMatingStats {
        int local = 0;
        int global = 0;
        int fallback = 0;
        int key_unknown = 0;
        int archive_local = 0;
        int archive_global = 0;
        int local_pool_samples = 0;
        int local_pool_min = 0;
        int local_pool_max = 0;
        long long local_pool_sum = 0;
        int active_quota_family_count = 0;
        int active_quota_per_family = 0;
        int active_quota_match = 0;
        int active_quota_allowed = 0;
        int active_quota_blocked = 0;
        int active_quota_max_family_used = 0;
        int active_plateau_quota_scaled_family_count = 0;
        int active_plateau_external_probe = 0;

        void record_local_pool(int pool_size) {
            if (local_pool_samples == 0) {
                local_pool_min = pool_size;
                local_pool_max = pool_size;
            } else {
                local_pool_min = std::min(local_pool_min, pool_size);
                local_pool_max = std::max(local_pool_max, pool_size);
            }
            local_pool_sum += pool_size;
            ++local_pool_samples;
        }

        void record_active_quota_use(int family_index, const std::vector<int>& family_used) {
            (void)family_index;
            ++active_quota_allowed;
            if (!family_used.empty()) {
                active_quota_max_family_used = std::max(
                    active_quota_max_family_used,
                    *std::max_element(family_used.begin(), family_used.end()));
            }
        }
    };

struct ActiveNicheStats {
        int family_count = 0;
        int member_count = 0;
        int free_count = 0;
        int min_quota = 0;
        int target_quota = 0;
        int family_cap = 0;
        int quota_deficit = 0;
        int below_min_count = 0;
        int max_family_size = 0;
        double max_family_fraction = std::numeric_limits<double>::quiet_NaN();
        double occupancy_gini = std::numeric_limits<double>::quiet_NaN();
        int floor_blocked = 0;
        int family_cap_blocked = 0;
        int deferred_candidates = 0;
        int repair_moves = 0;
        int rebalance_candidates = 0;
        int rebalance_moves = 0;
        int focus_family_count = 0;
        int focus_member_count = 0;
        int focus_target_quota = 0;
        int focus_family_cap = 0;
        int monitor_family_cap = 0;
        int focus_ready_count = 0;
        int focus_cooldown_family_count = 0;
        int focus_stopped_family_count = 0;
        double focus_last_rel_improvement =
            std::numeric_limits<double>::quiet_NaN();
    };

enum class PBestGuideSource {
        Self,
        SameKeyPopulation,
        BasinArchive,
        GlobalPopulation,
        None,
    };

struct PBestGuideSelection {
        bool has_guide = false;
        int population_index = -1;
        const std::vector<double>* scaled_values = nullptr;
        PBestGuideSource source = PBestGuideSource::None;
        std::string guide_key;
        bool active_plateau_suppressed = false;
        bool active_plateau_global_rescue = false;
    };

struct DeMatingSelection {
        int r0 = -1;
        int r1 = -1;
        int r2_pool = -1;
        bool used_local = false;
        bool archive_r2 = false;
    };

struct DeMatingPlan {
        bool request_local = false;
        DeMatingScope local_scope = DeMatingScope::Global;
        bool external_probe = false;
        std::string external_probe_family_key;
    };

struct ActiveMatingQuotaState {
        bool configured = false;
        bool enabled = false;
        std::vector<std::string> family_keys;
        std::vector<int> used;
        std::vector<int> family_limits;
        std::vector<char> stalled;
        int per_family_limit = 0;
        int stalled_family_count = 0;
    };

struct ActiveFamilyCoverage {
        std::vector<std::string> family_keys;
        std::vector<int> family_for_population;
        std::vector<int> family_counts;
        int family_limit = 0;
        int max_family_size = 0;
    };

struct ActiveNicheGenerationState {
        bool configured = false;
        bool survival_enabled = false;
        std::vector<std::string> family_keys;
        std::vector<int> member_counts;
        std::vector<int> family_target_quotas;
        std::vector<int> family_caps;
        std::vector<char> focus_family;
        int min_quota = 0;
        int target_quota = 0;
        int family_cap = 0;
        int focus_target_quota = 0;
        int focus_family_cap = 0;
        int monitor_family_cap = 0;
        int focus_family_count = 0;
        int focus_member_count = 0;
        int member_count = 0;
        int free_count = 0;
        int quota_deficit = 0;
        int below_min_count = 0;
        int max_family_size = 0;
        double max_family_fraction = std::numeric_limits<double>::quiet_NaN();
        double occupancy_gini = std::numeric_limits<double>::quiet_NaN();
    };

struct DeferredNicheCandidate {
        int source_index = -1;
        int niche_id = -1;
        std::vector<double> scaled;
        CandidateFitness fitness;
        double sampled_f = 0.0;
        double sampled_cr = 0.0;
    };

struct LocalMatingPools {
        std::vector<std::string> population_keys;
        std::vector<std::string> active_cohort_keys;
        std::vector<std::string> archive_keys;
        std::unordered_map<std::string, std::vector<int>> population_by_key;
        std::unordered_map<std::string, std::vector<int>> active_cohort_by_key;
        std::unordered_map<std::string, std::vector<int>> archive_by_key;
        std::unordered_map<std::string, std::vector<int>> basin_population_cache;
        std::unordered_map<std::string, std::vector<int>> basin_archive_cache;
        std::unordered_map<std::string, std::vector<int>> same_key_pbest_cache;
        std::unordered_map<std::string, std::vector<int>> basin_pbest_cache;
        int active_niche_member_count = 0;
        int active_focus_member_count = 0;
        int active_focus_target_quota = 0;
    };

struct ActiveFamilyAnchor {
        int representative_slot = -1;
        int anchor_count = 0;
        std::string key;
        std::vector<double> row;
    };

struct CohortCandidate {
                    double distance = std::numeric_limits<double>::infinity();
                    double margin = -std::numeric_limits<double>::infinity();
                    double merit = std::numeric_limits<double>::infinity();
                    bool same_family = false;
                    int family = -1;
                    int index = -1;
                };

struct FocusFillCandidate {
                std::vector<double> scaled;
                CandidateFitness fitness;
                std::string key;
                int source_index = -1;
                int source_priority = 0;
            };

struct FocusStopReleaseCandidate {
                std::vector<double> scaled;
                CandidateFitness fitness;
                std::string key;
                int niche_id = -1;
                int source_index = -1;
                int source_priority = 0;
            };

struct ActiveNicheRebalanceCandidate {
                    std::vector<double> scaled;
                    CandidateFitness fitness;
                    int niche_id = -1;
                    int source_index = -1;
                };

struct AdaptivePolishCandidate {
            int population_index = -1;
            std::vector<double> scaled;
            CandidateFitness fitness;
            std::string source;
            std::string topology_key;
            std::string descriptor_key;
            std::vector<std::uint64_t> descriptor_axis_values;
        };

struct AdaptivePolishResult {
            std::size_t candidate_index = 0;
            OptimizationResult polished;
            std::vector<double> polished_scaled;
            CandidateFitness fitness;
            bool completed = false;
            std::exception_ptr error;
        };

struct MicroIslandSummary {
            int requested_runs = 0;
            int runs = 0;
            int improvements = 0;
            int archived = 0;
            int early_stopped = 0;
            int retired = 0;
            int seed_count = 0;
            double seconds = 0.0;
            double de_seconds = 0.0;
            double polish_seconds = 0.0;
            bool used_cuda = false;
            std::vector<MicroIslandTraceRecord> records;
        };

struct MicroRetireDecision {
                bool retired = false;
                bool unique = false;
                std::string action = "not_retired";
                std::string key;
            };

struct BasinRefreshParentPool {
                        std::vector<double> population;
                        std::vector<std::vector<int>> topology_parent_indices;
                        std::vector<std::vector<int>> family_parent_indices;
                    };

} } // namespace optiland::optimizer_detail
