// Coalesce island population requests into the shared CUDA evaluator and return per-island slices.
// The broker owns its queue, worker lifetime, completion synchronization, and certificate transport.
// Preserve batching row maps, caller context, lock/wakeup order, and result ownership across threads.

#include "optiland/population_broker.hpp"
#include "optiland/evaluation_certificate.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace optiland {

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_seconds(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

void add_cuda_timing(CudaEvaluationTiming& target, const CudaEvaluationTiming& source) {
    target.calls += source.calls;
    target.candidates += source.candidates;
    target.context_seconds += source.context_seconds;
    target.host_setup_seconds += source.host_setup_seconds;
    target.host_to_device_seconds += source.host_to_device_seconds;
    target.kernel_seconds += source.kernel_seconds;
    target.device_to_host_seconds += source.device_to_host_seconds;
    target.cleanup_seconds += source.cleanup_seconds;
    target.total_seconds += source.total_seconds;
}

}  // namespace

class CudaBatchedPopulationBroker::Impl {
public:
    struct Request {
        int island_index = -1;
        std::uint64_t logical_call_index = 0;
        int population_size = 0;
        bool need_score_summaries = false;
        std::vector<double> rows;
        EvaluationCallerContext certificate_context;
        std::vector<EvaluationCertificate> certificate_response;
        Clock::time_point enqueued_at;
        std::mutex completion_mutex;
        std::condition_variable completion_cv;
        bool completed = false;
        PopulationEvaluationResult result;
    };

    Impl(
        std::vector<PopulationEvaluationIslandContext> island_contexts,
        MeritConfig merit_value,
        BatchedPopulationBrokerOptions options_value)
        : merit_config(std::move(merit_value)),
          options(options_value) {
        if (island_contexts.empty()) {
            throw std::invalid_argument("batched CUDA broker requires an island context");
        }
        base = island_contexts.front().base;
        variables = island_contexts.front().variables;
        original_dimension = static_cast<int>(variables.size());
        if (original_dimension <= 0) {
            throw std::invalid_argument("batched CUDA broker requires variables");
        }
        build_glass_layout(island_contexts);
        build_canonical_layout(island_contexts);
        options.coalesce_microseconds = std::max(0, options.coalesce_microseconds);
        options.maximum_batch_candidates =
            std::max(1, options.maximum_batch_candidates);
        worker = std::thread(&Impl::run, this);
    }

    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            stopping = true;
        }
        queue_cv.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
    }

    PopulationEvaluationResult evaluate(
        int island_index,
        std::uint64_t logical_call_index,
        const std::vector<double>& physical_population,
        int population_size,
        bool need_score_summaries) {
        if (population_size <= 0 || original_dimension <= 0 ||
            physical_population.size() !=
                static_cast<std::size_t>(population_size) * original_dimension) {
            PopulationEvaluationResult invalid;
            invalid.error = "invalid population shape for batched CUDA broker";
            return invalid;
        }
        if (population_size > options.maximum_batch_candidates) {
            PopulationEvaluationResult invalid;
            invalid.error = "one logical request exceeds maximum_batch_candidates";
            return invalid;
        }

        auto request = std::make_shared<Request>();
        request->island_index = island_index;
        request->logical_call_index = logical_call_index;
        request->population_size = population_size;
        request->need_score_summaries = need_score_summaries;
        request->certificate_context=evaluation_local_context();
        if (!evaluation_request_valid(request->certificate_context,population_size,need_score_summaries)) {
            evaluation_local_set_context(0,{},0.0);
            PopulationEvaluationResult invalid;
            invalid.error="Invalid request-owned certificate context";
            return invalid;
        }
        const auto hidden_it = island_hidden_values.find(island_index);
        if (hidden_it == island_hidden_values.end()) {
            PopulationEvaluationResult invalid;
            invalid.error = "island is not registered with the batched CUDA broker";
            return invalid;
        }
        const std::vector<double>& hidden = hidden_it->second;
        const auto glass_maps_it = island_glass_value_maps.find(island_index);
        if (glass_maps_it == island_glass_value_maps.end()) {
            PopulationEvaluationResult invalid;
            invalid.error = "island GlassChoice map is not registered with the batched CUDA broker";
            return invalid;
        }
        const auto& glass_maps = glass_maps_it->second;
        request->rows.reserve(
            static_cast<std::size_t>(population_size) * variables.size());
        for (int row = 0; row < population_size; ++row) {
            const auto begin = physical_population.begin() +
                static_cast<std::ptrdiff_t>(row) * original_dimension;
            for (int column = 0; column < original_dimension; ++column) {
                double value = begin[column];
                const auto& local_to_canonical =
                    glass_maps[static_cast<std::size_t>(column)];
                if (!local_to_canonical.empty()) {
                    const int local_index = std::clamp(
                        static_cast<int>(std::llround(value)),
                        0,
                        static_cast<int>(local_to_canonical.size()) - 1);
                    value = static_cast<double>(
                        local_to_canonical[static_cast<std::size_t>(local_index)]);
                }
                request->rows.push_back(value);
            }
            request->rows.insert(request->rows.end(), hidden.begin(), hidden.end());
        }
        request->enqueued_at = Clock::now();
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (stopping) {
                PopulationEvaluationResult stopped;
                stopped.error = "batched CUDA broker is stopping";
                return stopped;
            }
            queue.push_back(request);
        }
        queue_cv.notify_one();

        std::unique_lock<std::mutex> completion_lock(request->completion_mutex);
        request->completion_cv.wait(
            completion_lock,
            [&request] { return request->completed; });
        if (request->result.success)
            evaluation_local_complete(request->certificate_context,request->certificate_response);
        else evaluation_local_set_context(0,{},0.0);
        return std::move(request->result);
    }

    BatchedPopulationBrokerStats get_stats() const {
        std::lock_guard<std::mutex> lock(stats_mutex);
        return broker_stats;
    }

private:
    static bool same_glass_candidate(
        const GlassCandidate& left,
        const GlassCandidate& right) {
        return left.reference == right.reference &&
            left.name == right.name &&
            same_number(left.material.nd, right.material.nd) &&
            same_number(left.material.vd, right.material.vd);
    }

    static bool same_number(double left, double right) {
        return left == right || (std::isnan(left) && std::isnan(right));
    }

    static const GlassChoiceGroup* find_glass_group(
        const GlassSearchContext& context,
        const VariableSpec& variable) {
        if (variable.glass_group >= 0) {
            for (const auto& group : context.groups) {
                if (group.id == variable.glass_group) {
                    return &group;
                }
            }
        }
        for (const auto& group : context.groups) {
            if (group.surface_index == variable.index) {
                return &group;
            }
        }
        return nullptr;
    }

    void build_glass_layout(
        const std::vector<PopulationEvaluationIslandContext>& contexts) {
        glass_search = contexts.front().glass_search;
        for (auto& group : glass_search.groups) {
            group.candidates.clear();
            group.neighbor_indices.clear();
        }
        for (const auto& context : contexts) {
            if (context.glass_search.enabled != glass_search.enabled ||
                context.glass_search.groups.size() != glass_search.groups.size()) {
                throw std::invalid_argument(
                    "incompatible island GlassChoice group layout for CUDA broker");
            }
            for (std::size_t group_index = 0;
                 group_index < glass_search.groups.size(); ++group_index) {
                const auto& local = context.glass_search.groups[group_index];
                const auto& canonical = glass_search.groups[group_index];
                if (local.id != canonical.id ||
                    local.surface_index != canonical.surface_index) {
                    throw std::invalid_argument(
                        "incompatible island GlassChoice id/surface layout for CUDA broker");
                }
            }
        }

        std::unordered_map<int, std::vector<std::vector<int>>> group_maps;
        for (const auto& context : contexts) {
            std::vector<std::vector<int>> local_group_maps(
                glass_search.groups.size());
            for (std::size_t group_index = 0;
                 group_index < glass_search.groups.size(); ++group_index) {
                const auto& local = context.glass_search.groups[group_index];
                auto& canonical = glass_search.groups[group_index];
                auto& local_map = local_group_maps[group_index];
                local_map.reserve(local.candidates.size());
                for (const auto& candidate : local.candidates) {
                    auto match = std::find_if(
                        canonical.candidates.begin(),
                        canonical.candidates.end(),
                        [&candidate](const GlassCandidate& existing) {
                            return same_glass_candidate(existing, candidate);
                        });
                    if (match == canonical.candidates.end()) {
                        canonical.candidates.push_back(candidate);
                        local_map.push_back(
                            static_cast<int>(canonical.candidates.size()) - 1);
                    } else {
                        local_map.push_back(static_cast<int>(
                            std::distance(canonical.candidates.begin(), match)));
                    }
                }
            }
            group_maps.emplace(context.island_index, std::move(local_group_maps));
        }

        for (const auto& context : contexts) {
            std::vector<std::vector<int>> variable_maps(
                static_cast<std::size_t>(original_dimension));
            const auto group_maps_it = group_maps.find(context.island_index);
            for (int variable_index = 0;
                 variable_index < original_dimension; ++variable_index) {
                const auto& variable =
                    context.variables[static_cast<std::size_t>(variable_index)];
                if (variable.kind != VariableKind::GlassChoice) {
                    continue;
                }
                const GlassChoiceGroup* local_group =
                    find_glass_group(context.glass_search, variable);
                if (local_group == nullptr) {
                    throw std::invalid_argument(
                        "GlassChoice variable has no island-local candidate group");
                }
                auto group_position = std::find_if(
                    context.glass_search.groups.begin(),
                    context.glass_search.groups.end(),
                    [local_group](const GlassChoiceGroup& group) {
                        return &group == local_group;
                    });
                const std::size_t group_index = static_cast<std::size_t>(
                    std::distance(context.glass_search.groups.begin(), group_position));
                variable_maps[static_cast<std::size_t>(variable_index)] =
                    group_maps_it->second[group_index];
            }
            island_glass_value_maps.emplace(
                context.island_index, std::move(variable_maps));
        }
    }

    bool surface_controlled(
        std::size_t surface_index,
        VariableKind first,
        VariableKind second,
        std::size_t term = 0) const {
        for (int i = 0; i < original_dimension; ++i) {
            const VariableSpec& var = variables[static_cast<std::size_t>(i)];
            if (var.index != surface_index ||
                (var.kind != first && var.kind != second)) {
                continue;
            }
            if (first != VariableKind::AsphereCoeff || var.term == term) {
                return true;
            }
        }
        return false;
    }

    bool field_controlled(
        std::size_t field_index,
        VariableKind first,
        VariableKind second) const {
        for (int i = 0; i < original_dimension; ++i) {
            const VariableSpec& var = variables[static_cast<std::size_t>(i)];
            if (var.index == field_index &&
                (var.kind == first || var.kind == second)) {
                return true;
            }
        }
        return false;
    }

    template <typename Getter>
    void add_hidden_if_different(
        const std::vector<PopulationEvaluationIslandContext>& contexts,
        std::size_t surface_index,
        VariableKind kind,
        Getter getter,
        std::size_t term = 0) {
        const double reference = getter(contexts.front().base.surfaces[surface_index]);
        bool different = false;
        double lower = reference;
        double upper = reference;
        for (const auto& context : contexts) {
            const double value = getter(context.base.surfaces[surface_index]);
            different = different || !same_number(reference, value);
            if (std::isfinite(value)) {
                lower = std::isfinite(lower) ? std::min(lower, value) : value;
                upper = std::isfinite(upper) ? std::max(upper, value) : value;
            }
        }
        if (!different) {
            return;
        }
        VariableSpec hidden;
        hidden.kind = kind;
        hidden.index = surface_index;
        hidden.term = term;
        hidden.initial = reference;
        hidden.lower = lower;
        hidden.upper = upper;
        variables.push_back(hidden);
        for (const auto& context : contexts) {
            island_hidden_values[context.island_index].push_back(
                getter(context.base.surfaces[surface_index]));
        }
    }

    template <typename Getter>
    void add_hidden_field_if_different(
        const std::vector<PopulationEvaluationIslandContext>& contexts,
        std::size_t field_index,
        VariableKind kind,
        Getter getter) {
        const double reference = getter(contexts.front().base.fields[field_index]);
        bool different = false;
        double lower = reference;
        double upper = reference;
        for (const auto& context : contexts) {
            const double value = getter(context.base.fields[field_index]);
            different = different || !same_number(reference, value);
            if (std::isfinite(value)) {
                lower = std::isfinite(lower) ? std::min(lower, value) : value;
                upper = std::isfinite(upper) ? std::max(upper, value) : value;
            }
        }
        if (!different) {
            return;
        }
        VariableSpec hidden;
        hidden.kind = kind;
        hidden.index = field_index;
        hidden.initial = reference;
        hidden.lower = lower;
        hidden.upper = upper;
        variables.push_back(hidden);
        for (const auto& context : contexts) {
            island_hidden_values[context.island_index].push_back(
                getter(context.base.fields[field_index]));
        }
    }

    void build_canonical_layout(
        const std::vector<PopulationEvaluationIslandContext>& contexts) {
        for (const auto& context : contexts) {
            if (context.variables.size() != static_cast<std::size_t>(original_dimension) ||
                context.base.surfaces.size() != base.surfaces.size() ||
                context.base.fields.size() != base.fields.size()) {
                throw std::invalid_argument("incompatible island layout for CUDA broker");
            }
            for (int index = 0; index < original_dimension; ++index) {
                const VariableSpec& reference =
                    variables[static_cast<std::size_t>(index)];
                const VariableSpec& candidate =
                    context.variables[static_cast<std::size_t>(index)];
                if (candidate.kind != reference.kind ||
                    candidate.index != reference.index ||
                    candidate.term != reference.term) {
                    throw std::invalid_argument(
                        "incompatible variable kind/index/term layout for CUDA broker");
                }
            }
            island_hidden_values.emplace(context.island_index, std::vector<double>{});
        }
        // Candidate rows have already been clipped by their owning optimizer.
        // A union clamp keeps the shared scorer from applying island 0's local
        // bounds to another island while retaining a finite safety interval.
        for (int index = 0; index < original_dimension; ++index) {
            VariableSpec& packed = variables[static_cast<std::size_t>(index)];
            for (const auto& context : contexts) {
                const VariableSpec& candidate =
                    context.variables[static_cast<std::size_t>(index)];
                if (std::isfinite(candidate.lower)) {
                    packed.lower = std::min(packed.lower, candidate.lower);
                }
                if (std::isfinite(candidate.upper)) {
                    packed.upper = std::max(packed.upper, candidate.upper);
                }
            }
            if (packed.kind == VariableKind::GlassChoice) {
                const GlassChoiceGroup* group =
                    find_glass_group(glass_search, packed);
                if (group == nullptr || group->candidates.empty()) {
                    throw std::invalid_argument(
                        "canonical GlassChoice group is empty");
                }
                packed.lower = 0.0;
                packed.upper = static_cast<double>(group->candidates.size() - 1);
            }
        }
        for (std::size_t surface = 0; surface < base.surfaces.size(); ++surface) {
            if (!surface_controlled(
                    surface, VariableKind::Radius, VariableKind::Curvature)) {
                add_hidden_if_different(
                    contexts, surface, VariableKind::Radius,
                    [](const Surface& value) { return value.radius; });
            }
            if (!surface_controlled(
                    surface, VariableKind::Thickness, VariableKind::Thickness)) {
                add_hidden_if_different(
                    contexts, surface, VariableKind::Thickness,
                    [](const Surface& value) { return value.thickness; });
            }
            const bool material_controlled =
                surface_controlled(
                    surface, VariableKind::MaterialNd, VariableKind::MaterialVd) ||
                surface_controlled(
                    surface, VariableKind::GlassChoice, VariableKind::GlassChoice);
            if (!material_controlled) {
                add_hidden_if_different(
                    contexts, surface, VariableKind::MaterialNd,
                    [](const Surface& value) { return value.material_after.nd; });
                add_hidden_if_different(
                    contexts, surface, VariableKind::MaterialVd,
                    [](const Surface& value) { return value.material_after.vd; });
                const std::size_t wave_count = std::min<std::size_t>(
                    base.wavelengths.empty() ? 1 : base.wavelengths.size(), 8);
                for (std::size_t wave_index = 0; wave_index < wave_count; ++wave_index) {
                    const double wavelength_um = base.wavelengths.empty()
                        ? 0.5875618
                        : base.wavelengths[wave_index].value_um;
                    add_hidden_if_different(
                        contexts,
                        surface,
                        VariableKind::EvaluatorMaterialIndex,
                        [wavelength_um](const Surface& value) {
                            return value.material_after.refractive_index(wavelength_um);
                        },
                        wave_index);
                }
            }
            if (!surface_controlled(
                    surface, VariableKind::Conic, VariableKind::Conic)) {
                add_hidden_if_different(
                    contexts, surface, VariableKind::Conic,
                    [](const Surface& value) { return value.conic; });
            }
            std::size_t coefficient_count = 0;
            for (const auto& context : contexts) {
                coefficient_count = std::max(
                    coefficient_count,
                    context.base.surfaces[surface].asphere_coefficients.size());
            }
            for (std::size_t term = 0; term < coefficient_count; ++term) {
                if (surface_controlled(
                        surface,
                        VariableKind::AsphereCoeff,
                        VariableKind::AsphereCoeff,
                        term)) {
                    continue;
                }
                add_hidden_if_different(
                    contexts, surface, VariableKind::AsphereCoeff,
                    [term](const Surface& value) {
                        return term < value.asphere_coefficients.size()
                            ? value.asphere_coefficients[term]
                            : 0.0;
                    },
                    term);
            }
            add_hidden_if_different(
                contexts, surface, VariableKind::EvaluatorSemiAperture,
                [](const Surface& value) { return value.semi_aperture; });
            add_hidden_if_different(
                contexts, surface, VariableKind::EvaluatorMechanicalAperture,
                [](const Surface& value) { return value.mechanical_aperture; });
            add_hidden_if_different(
                contexts, surface, VariableKind::EvaluatorApertureRadius,
                [](const Surface& value) { return value.aperture_radius; });
        }
        // The shared CUDA context starts from island 0's field array.  Pack
        // every island-local field coordinate that is not already an optimizer
        // dimension, just as the surface loop above does for geometry and
        // apertures.  Without this, heterogeneous vignetting maps are silently
        // scored using island 0's pupil and only fail when materialized locally.
        for (std::size_t field = 0; field < base.fields.size(); ++field) {
            add_hidden_field_if_different(
                contexts, field, VariableKind::EvaluatorFieldHx,
                [](const Field& value) { return value.hx; });
            add_hidden_field_if_different(
                contexts, field, VariableKind::EvaluatorFieldHy,
                [](const Field& value) { return value.hy; });
            add_hidden_field_if_different(
                contexts, field, VariableKind::EvaluatorVignetteCenterX,
                [](const Field& value) { return value.fvcx; });
            if (!field_controlled(
                    field, VariableKind::VignetteX, VariableKind::VignetteX)) {
                add_hidden_field_if_different(
                    contexts, field, VariableKind::VignetteX,
                    [](const Field& value) { return value.fvdx; });
            }
            if (!field_controlled(
                    field,
                    VariableKind::VignetteY,
                    VariableKind::VignetteMagnitudeY)) {
                add_hidden_field_if_different(
                    contexts, field, VariableKind::VignetteY,
                    [](const Field& value) { return value.fvdy; });
            }
            if (!field_controlled(
                    field,
                    VariableKind::VignetteCenterY,
                    VariableKind::VignetteAsymmetryY)) {
                add_hidden_field_if_different(
                    contexts, field, VariableKind::VignetteCenterY,
                    [](const Field& value) { return value.fvcy; });
            }
        }
    }

    void complete(
        const std::shared_ptr<Request>& request,
        PopulationEvaluationResult result) {
        {
            std::lock_guard<std::mutex> lock(request->completion_mutex);
            request->result = std::move(result);
            request->completed = true;
        }
        request->completion_cv.notify_one();
    }

    void run() {
        // This worker alone touches the legacy CUDA global context. Caller TLS
        // is explicitly captured, packed, split and published through Request.
        while (true) {
            EvaluationCallerContext packed_context;
            std::vector<std::shared_ptr<Request>> batch;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this] { return stopping || !queue.empty(); });
                if (stopping && queue.empty()) {
                    break;
                }

                const auto deadline = Clock::now() +
                    std::chrono::microseconds(options.coalesce_microseconds);
                while (!stopping && Clock::now() < deadline) {
                    // When the caller sets the maximum to one complete logical
                    // wave (for example five P900 direction scouts), launch as
                    // soon as that wave is present.  The old loop always slept
                    // through the full coalescing interval, even after a full
                    // batch had arrived, so a long straggler-tolerant window
                    // also became fixed per-generation latency.
                    const bool need_summaries = queue.front()->need_score_summaries;
                    int compatible_rows = 0;
                    EvaluationCallerContext compatible_context;
                    for (const auto& request : queue) {
                        if (request->need_score_summaries == need_summaries &&
                            evaluation_context_compatible(compatible_context,request->certificate_context)) {
                            if (request->certificate_context.mode) {
                                compatible_context.mode=request->certificate_context.mode;
                                compatible_context.epsilon=request->certificate_context.epsilon;
                            }
                            compatible_rows += request->population_size;
                            if (compatible_rows >=
                                options.maximum_batch_candidates) {
                                break;
                            }
                        }
                    }
                    if (compatible_rows >= options.maximum_batch_candidates) {
                        break;
                    }
                    queue_cv.wait_until(lock, deadline);
                }

                const bool need_summaries = queue.front()->need_score_summaries;
                int candidate_rows = 0;
                for (auto it = queue.begin(); it != queue.end();) {
                    const auto& request = *it;
                    if (request->need_score_summaries != need_summaries ||
                        !evaluation_context_compatible(packed_context,request->certificate_context) ||
                        candidate_rows + request->population_size >
                            options.maximum_batch_candidates) {
                        ++it;
                        continue;
                    }
                    if (request->certificate_context.mode) {
                        packed_context.mode=request->certificate_context.mode;
                        packed_context.epsilon=request->certificate_context.epsilon;
                    }
                    candidate_rows += request->population_size;
                    batch.push_back(request);
                    it = queue.erase(it);
                }
            }

            std::stable_sort(
                batch.begin(),
                batch.end(),
                [](const auto& left, const auto& right) {
                    return std::pair<int, std::uint64_t>(
                               left->island_index, left->logical_call_index) <
                           std::pair<int, std::uint64_t>(
                               right->island_index, right->logical_call_index);
                });

            int total_rows = 0;
            std::vector<double> packed_rows;
            for (const auto& request : batch) {
                total_rows += request->population_size;
                packed_rows.insert(
                    packed_rows.end(), request->rows.begin(), request->rows.end());
                // Same sorted requests and row offset as optical physical data.
                if (request->certificate_context.mode)
                    packed_context.rows.insert(packed_context.rows.end(),
                        request->certificate_context.rows.begin(),request->certificate_context.rows.end());
                else packed_context.rows.insert(packed_context.rows.end(),request->population_size,EvaluationCertificate{});
            }

            const auto launch_start = Clock::now();
            std::vector<double> merits;
            std::vector<CudaScoreSummary> summaries;
            CudaEvaluationTiming cuda_timing;
            bool ok = false;
            std::string error;
            std::vector<EvaluationCertificate> proofs;
            try {
                evaluation_set_certificate_context(packed_context.mode,
                    packed_context.mode ? packed_context.rows : std::vector<EvaluationCertificate>{},packed_context.epsilon);
                if (!batch.empty() && batch.front()->need_score_summaries) {
                    ok = evaluate_population_cuda_scores(
                        base,
                        variables,
                        merit_config,
                        packed_rows,
                        total_rows,
                        &merits,
                        &summaries,
                        &cuda_timing,
                        &glass_search);
                } else {
                    ok = evaluate_population_cuda(
                        base,
                        variables,
                        merit_config,
                        packed_rows,
                        total_rows,
                        &merits,
                        &cuda_timing,
                        &glass_search);
                }
                if (ok && packed_context.mode) {
                    proofs=evaluation_certificate_results();
                    if (proofs.size()!=static_cast<std::size_t>(total_rows))
                        throw std::runtime_error("Certificate packed result shape mismatch");
                    for (int row=0;row<total_rows;++row)
                        if (!evaluation_parent_identity_equal(packed_context.rows[row],proofs[row]) ||
                            ((!packed_context.rows[row].eligible && !packed_context.rows[row].merit_eligible) && proofs[row].reason))
                            throw std::runtime_error("Certificate row identity or zero-mask invariant violated");
                }
                if (!ok) error = "CUDA scorer rejected packed population";
            } catch (const std::exception& exc) {
                ok=false;
                error = exc.what();
            } catch (...) {
                ok=false;
                error = "unknown CUDA scorer exception";
            }

            // Clear before releasing any caller, on success and all exceptions.
            evaluation_set_certificate_context(0,{},0.0);
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                broker_stats.logical_requests += static_cast<long long>(batch.size());
                ++broker_stats.physical_launches;
                broker_stats.merged_requests +=
                    std::max<long long>(0, static_cast<long long>(batch.size()) - 1);
                broker_stats.candidate_rows += total_rows;
                broker_stats.maximum_batch_rows =
                    std::max(broker_stats.maximum_batch_rows, total_rows);
                broker_stats.wall_seconds += elapsed_seconds(launch_start);
                add_cuda_timing(broker_stats.cuda, cuda_timing);
                if(ok) for(const auto& proof:proofs) {
                    broker_stats.certificate_eligible += proof.eligible!=0;
                    broker_stats.merit_certificate_eligible += proof.merit_eligible!=0;
                    if(proof.reason==1)++broker_stats.trace_feasible_parent_proofs;
                    if(proof.reason==2)++broker_stats.trace_infeasible_parent_proofs;
                    if(proof.reason==3)++broker_stats.rms_proofs;
                    if(packed_context.mode==2 && proof.reason>0)++broker_stats.certified_rejections;
                }
                for (const auto& request : batch) {
                    broker_stats.queue_wait_seconds +=
                        std::chrono::duration<double>(
                            launch_start - request->enqueued_at).count();
                }
            }

            int offset = 0;
            for (const auto& request : batch) {
                PopulationEvaluationResult result;
                result.success = ok;
                result.used_cuda = ok;
                result.error = error;
                if (ok && offset + request->population_size <=
                              static_cast<int>(merits.size())) {
                    result.merits.assign(
                        merits.begin() + offset,
                        merits.begin() + offset + request->population_size);
                    if (request->need_score_summaries &&
                        offset + request->population_size <=
                            static_cast<int>(summaries.size())) {
                        result.summaries.assign(
                            summaries.begin() + offset,
                            summaries.begin() + offset + request->population_size);
                    } else if (request->need_score_summaries) {
                        result.success = false;
                        result.used_cuda = false;
                        result.error = "CUDA scorer returned the wrong summary count";
                    }
                } else if (ok) {
                    result.success = false;
                    result.used_cuda = false;
                    result.error = "CUDA scorer returned the wrong merit count";
                }
                if (ok && packed_context.mode && request->certificate_context.mode)
                    request->certificate_response.assign(proofs.begin()+offset,proofs.begin()+offset+request->population_size);
                offset += request->population_size;
                complete(request, std::move(result));
            }
        }
    }

    LensSystem base;
    std::vector<VariableSpec> variables;
    MeritConfig merit_config;
    GlassSearchContext glass_search;
    BatchedPopulationBrokerOptions options;
    int original_dimension = 0;
    std::unordered_map<int, std::vector<double>> island_hidden_values;
    std::unordered_map<int, std::vector<std::vector<int>>> island_glass_value_maps;

    mutable std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<std::shared_ptr<Request>> queue;
    bool stopping = false;
    std::thread worker;

    mutable std::mutex stats_mutex;
    BatchedPopulationBrokerStats broker_stats;
};

CudaBatchedPopulationBroker::CudaBatchedPopulationBroker(
    LensSystem base,
    std::vector<VariableSpec> variables,
    MeritConfig merit_config,
    GlassSearchContext glass_search,
    BatchedPopulationBrokerOptions options)
    : impl_(std::make_unique<Impl>(
          std::vector<PopulationEvaluationIslandContext>{
              {0, std::move(base), std::move(variables), std::move(glass_search)}},
          std::move(merit_config),
          options)) {}

CudaBatchedPopulationBroker::CudaBatchedPopulationBroker(
    std::vector<PopulationEvaluationIslandContext> island_contexts,
    MeritConfig merit_config,
    BatchedPopulationBrokerOptions options)
    : impl_(std::make_unique<Impl>(
          std::move(island_contexts),
          std::move(merit_config),
          options)) {}

CudaBatchedPopulationBroker::~CudaBatchedPopulationBroker() = default;

PopulationEvaluationResult CudaBatchedPopulationBroker::evaluate(
    int island_index,
    std::uint64_t logical_call_index,
    const std::vector<double>& physical_population,
    int population_size,
    bool need_score_summaries) {
    return impl_->evaluate(
        island_index,
        logical_call_index,
        physical_population,
        population_size,
        need_score_summaries);
}

BatchedPopulationBrokerStats CudaBatchedPopulationBroker::stats() const {
    return impl_->get_stats();
}

}  // namespace optiland
