#pragma once
#include "../prelude.cuh"
#include "../configuration/runtime_options.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "../diagnostics/builder_instrumentation.cuh"
#include "kernel_pipeline.cuh"
#include "../merit/supported_ray_generation.cuh"

namespace optiland {
class PopulationScoringExecution {
public:
    PopulationScoringExecution(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const MeritConfig& merit_config,
    const std::vector<double>& flat_population,
    int population_size,
    std::vector<double>* merits,
    std::vector<CudaScoreSummary>* summaries,
    CudaEvaluationTiming* timing,
    const GlassSearchContext* glass_search)
        : base(base), variables(variables), merit_config(merit_config), flat_population(flat_population), population_size(population_size), merits(merits), summaries(summaries), timing(timing), glass_search(glass_search) {}
    bool run();

private:
    const LensSystem& base;
    const std::vector<VariableSpec>& variables;
    const MeritConfig& merit_config;
    const std::vector<double>& flat_population;
    int population_size;
    std::vector<double>* merits;
    std::vector<CudaScoreSummary>* summaries;
    CudaEvaluationTiming* timing;
    const GlassSearchContext* glass_search;

    struct CudaStatusAdapter {
        PopulationScoringExecution* owner;
        bool operator()(cudaError_t status) const;
    };
    CudaStatusAdapter cuda_status_ok{this};

    bool geometry_only{};
    bool full_scorer{};
    bool debug_reject{};
    std::size_t field_count{};
    std::size_t wavelength_count{};
    std::size_t glass_proximity_point_count{};
    std::vector<DeviceVariable> host_variables{};
    std::vector<DeviceGlassCandidate> host_glass_candidates{};
    CudaEvaluationTiming local_timing{};
    bool memory_debug_enabled{};
    std::size_t memory_free_before{};
    std::size_t memory_total{};
    std::size_t memory_free_after_alloc{};
    std::size_t memory_free_before_cleanup{};
    std::size_t memory_free_after_cleanup{};
    cudaEvent_t start{};
    cudaEvent_t after_setup{};
    cudaEvent_t after_h2d{};
    cudaEvent_t after_kernel{};
    cudaEvent_t after_d2h{};
    cudaEvent_t after_cleanup{};
    bool stage_timing_enabled{};
    cudaEvent_t after_geometry{};
    cudaEvent_t after_initial_models{};
    cudaEvent_t after_aperture{};
    cudaEvent_t after_final_models{};
    cudaEvent_t after_spot_trace{};
    cudaEvent_t after_chief_trace{};
    cudaEvent_t after_spot_reduce{};
    cudaEvent_t after_wavefront_guard{};
    cudaEvent_t after_wavefront_stats{};
    cudaEvent_t after_spot{};
    cudaEvent_t after_merit{};
    DeviceConfig cfg{};
    bool uc5_experiment_scope{};
    bool uc5_combined{};
    bool uc5_symmetry_enabled{};
    bool uc5_primary_abort_enabled{};
    bool uc5_candidate_batch_enabled{};
    bool asphere_uc5_profile_enabled{};
    unsigned long long uc5_evaluation_index{};
    const char * asphere_feasibility_profile_override{};
    bool asphere_feasibility_profile_enabled{};
    bool asphere_deep_profile_enabled{};
    bool asphere_root_profile_enabled{};
    AsphereFeasibilityHostTiming asphere_feasibility_timing{};
    const char * transpose_override{};
    bool transpose_ray_tasks{};
    int constraint_debug_row{};
    bool constraint_debug_enabled{};
    int trace_debug_row{};
    bool trace_debug_enabled{};
    std::vector<DeviceSurface> host_surfaces{};
    std::vector<DeviceField> host_fields{};
    std::vector<DeviceWave> host_waves{};
    bool enable_calibrated_safeguarded_recovery{};
    std::vector<double> host_material_indices{};
    std::vector<DeviceGlassProximity> host_glass_proximity{};
    std::vector<DeviceGlassProximityPoint> host_glass_proximity_points{};
    std::vector<CudaScoreSummary> host_summaries{};
    cudaError_t first_cuda_error{};
    DeviceSurface * d_surfaces{};
    DeviceVariable * d_variables{};
    DeviceGlassCandidate * d_glass_candidates{};
    DeviceField * d_fields{};
    DeviceWave * d_waves{};
    double * d_material_indices{};
    DeviceGlassProximity * d_glass_proximity{};
    DeviceGlassProximityPoint * d_glass_proximity_points{};
    double * d_population{};
    DeviceSurface * d_candidate_surfaces{};
    DeviceField * d_candidate_fields{};
    double * d_aperture_max_heights{};
    double * d_meridional_boundary_heights{};
    DeviceCalibratedModel * d_calibrated_models{};
    EvaluationCertificate * d_evaluation_certificate{};
    DeviceBuilderDiagnostic * d_builder_diagnostic{};
    const char * builder_csv_path{};
    int edge_path_mode{};
    int edge_path_shadow{};
    int builder_diag_level{};
    DeviceSpotRaySample * d_spot_ray_samples{};
    double * d_pupil_order_heights{};
    DeviceSpotRaySample * d_wavefront_guard_samples{};
    DeviceSpotRaySample * d_spot_tail_extra_edge_samples{};
    DeviceChiefRaySample * d_chief_ray_samples{};
    DeviceFieldSpotStats * d_field_spot_stats{};
    DeviceFieldWavefrontStats * d_field_wavefront_stats{};
    DeviceMeritTerms * d_merit_terms{};
    CudaScoreSummary * d_summaries{};
    double * d_constraint_debug{};
    DeviceAsphereRootProfile * d_asphere_root_profile{};
    DeviceAsphereRootProfile host_asphere_root_profile{};
    DeviceAsphereRootProfile root_profile_after_geometry{};
    DeviceAsphereRootProfile root_profile_after_initial_models{};
    DeviceAsphereRootProfile root_profile_after_aperture{};
    DeviceAsphereRootProfile root_profile_after_final_models{};
    DeviceAsphereFeasibilityProfile * d_asphere_feasibility_profile{};
    DeviceAsphereFeasibilityProfile host_asphere_feasibility_profile{};
    DeviceAsphereDeepProfile * d_asphere_deep_profile{};
    DeviceAsphereDeepProfile host_asphere_deep_profile{};
    DeviceAsphereUC5Profile * d_asphere_uc5_profile{};
    DeviceAsphereUC5Profile host_asphere_uc5_profile{};
    DeviceUC5CandidateFamilyState * d_uc5_candidate_states{};
    std::size_t surface_bytes{};
    std::size_t variable_bytes{};
    std::size_t glass_candidate_bytes{};
    std::size_t field_bytes{};
    std::size_t wave_bytes{};
    std::size_t material_index_bytes{};
    std::size_t glass_proximity_bytes{};
    std::size_t glass_proximity_point_bytes{};
    std::size_t population_bytes{};
    std::size_t candidate_surface_bytes{};
    std::size_t candidate_field_bytes{};
    bool has_candidate_field_variables{};
    std::size_t aperture_max_heights_bytes{};
    std::size_t meridional_boundary_heights_bytes{};
    std::size_t calibrated_model_count{};
    std::size_t calibrated_model_bytes{};
    std::size_t uc5_candidate_state_bytes{};
    std::size_t field_spot_stats_bytes{};
    int codev_pupil_count{};
    int base_spot_pupil_count{};
    int elevated_spot_pupil_count{};
    int spot_pupil_count{};
    std::size_t spot_ray_sample_count{};
    std::size_t spot_ray_samples_bytes{};
    std::size_t pupil_order_heights_bytes{};
    std::size_t wavefront_guard_sample_count{};
    std::size_t wavefront_guard_samples_bytes{};
    std::size_t spot_tail_extra_edge_sample_count{};
    std::size_t spot_tail_extra_edge_samples_bytes{};
    std::size_t chief_ray_sample_count{};
    std::size_t chief_ray_samples_bytes{};
    std::size_t field_wavefront_stats_bytes{};
    std::size_t merit_terms_bytes{};
    std::size_t summary_bytes{};
    std::size_t constraint_debug_bytes{};
    std::vector<double> host_constraint_debug{};
    std::vector<DeviceFieldSpotStats> host_trace_debug_fields{};
    std::vector<DeviceFieldWavefrontStats> host_trace_debug_wavefront_fields{};
    std::vector<DeviceSpotRaySample> host_trace_debug_samples{};
    std::vector<DeviceCalibratedModel> host_trace_debug_models{};
    bool ok{};
    int uc5_symmetry_flag{};
    int uc5_primary_abort_flag{};
    int uc5_candidate_batch_flag{};
    DeviceAsphereUC5Profile * null_uc5_profile{};
    bool calibrated_model_cache{};
    bool reuse_pre_aperture_models{};
    const char * evaluation_ordered{};
    bool evaluation_supported{};
    bool parallel_ray_trace{};
    bool parallel_aperture_trace{};
    bool needs_vignetting_boundary_support{};
    bool parallel_spot_trace{};
    bool fuse_merit_aperture_envelope{};
    bool needs_pupil_order_history{};
    bool needs_wavefront{};
    bool needs_single_ray_spot_guard{};
    bool needs_working_f_number{};
    bool needs_selective_spot_tail_edges{};
    bool track_unused_opd{};
    bool needs_chief_samples{};
    bool aperture_y_guards_unrestricted{};
    bool reuse_aperture_y_guards{};
    int calibrated_center_field_index{};

    bool reject(const char* reason);
    bool track_cuda_status(cudaError_t status);
    void print_asphere_deep_profile();
    native_detail::PhaseOutcome validate_inputs();
    native_detail::PhaseOutcome prepare_host_configuration();
    native_detail::PhaseOutcome prepare_device_context();
    native_detail::PhaseOutcome allocate_device_buffers();
    native_detail::PhaseOutcome upload_inputs();
    native_detail::PhaseOutcome launch_kernels();
    native_detail::PhaseOutcome readback_results();
    native_detail::PhaseOutcome report_diagnostics();
    native_detail::PhaseOutcome release_device_resources();
    native_detail::PhaseOutcome finish_call();
};
} // namespace optiland
