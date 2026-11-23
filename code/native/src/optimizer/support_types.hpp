#pragma once
#include "optiland/optimizer.hpp"
#include "optiland/evaluation_certificate.hpp"

#include "optiland/analysis.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif


namespace optiland { namespace optimizer_detail {

using Clock = std::chrono::steady_clock;

class PortableMutex {
public:
    void lock() noexcept {
        while (locked_.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock() noexcept {
        locked_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag locked_ = ATOMIC_FLAG_INIT;
};

constexpr double kTopologyWeakSurfacePower = 0.12;

constexpr double kTopologyWeakElementPower = 0.10;

constexpr double kTopologyWeakGroupPower = 0.06;

struct OpticalElementDescriptor {
    char s1 = 'z';
    char s2 = 'z';
    char power_sign = 'w';
    char power_mag = 'w';
    char abs_share = '0';
    char bending = 'x';
    char material_nd = 'x';
    char material_vd = 'x';
    char thickness = 'x';
    double power = 0.0;
    double abs_power = 0.0;
    std::uint64_t shape_hash = 0;
    std::uint64_t power_hash = 0;
    std::uint64_t shape_power_hash = 0;
    std::uint64_t semantic_hash = 0;
};

struct OpticalWindowDescriptor {
    int start = 0;
    int length = 0;
    char net_power_sign = 'w';
    char abs_share = '0';
    char centroid = 'f';
    char alternation = '0';
    std::uint64_t shape_seq_hash = 0;
    std::uint64_t power_seq_hash = 0;
    std::uint64_t shape_power_hash = 0;
};

struct OpticalTopologyDescriptor {
    int element_count = 0;
    int stop_after_element = -1;
    std::uint64_t global_power_hash = 0;
    std::uint64_t spacing_hash = 0;
    std::uint64_t diagnostic_hash = 0;
    std::vector<OpticalElementDescriptor> elements;
    std::vector<OpticalWindowDescriptor> windows;
    std::vector<std::uint64_t> axis_values;
    std::string key;
    std::string diagnostic;
};

struct ContinuousStructureSignature {
    int element_count = 0;
    int stop_after_elements = -1;
    std::vector<double> element_powers;
    std::vector<double> surface_curvatures;
    std::vector<double> glass_thicknesses;
    std::vector<double> air_gaps;
};

struct ContinuousAvoidMatch {
    bool matched = false;
    double score = 0.0;
};

struct ParsedStructuralTopologyKey {
    std::string element_count;
    std::string stop_after_elements;
    std::string front_power_sign;
    std::string rear_power_sign;
    std::vector<std::string> element_tokens;
};

struct PolishWorkResult {
    int index = -1;
    double before_merit = std::numeric_limits<double>::infinity();
    std::vector<double> polished_scaled;
    Score score;
    bool improved = false;
    bool used_cuda = false;
};

struct MicroDlsInput {
    int index = -1;
    std::vector<double> scaled;
    double before_merit = std::numeric_limits<double>::infinity();
};

struct PolishThreadContext {
    const DifferentialEvolution* optimizer = nullptr;
    const PolishOptions* options = nullptr;
    const std::vector<VariableSpec>* variables = nullptr;
    int index = -1;
    double before_merit = std::numeric_limits<double>::infinity();
    bool rollback = true;
    std::vector<double> before_values;
    PolishWorkResult result;
    std::exception_ptr error;
};

#ifdef _WIN32
struct FunctionThreadContext {
    std::function<void()> fn;
    std::exception_ptr error;
};
#endif

#ifdef _WIN32
struct CpuPopulationEvalThreadContext {
    const LensSystem* base = nullptr;
    const std::vector<VariableSpec>* variables = nullptr;
    const GlassSearchContext* glass_search = nullptr;
    const MeritConfig* merit_config = nullptr;
    const std::vector<double>* flat_population = nullptr;
    std::vector<double>* merits = nullptr;
    int dim = 0;
    int population_size = 0;
    volatile LONG next_index = 0;
    CRITICAL_SECTION error_lock;
    std::exception_ptr first_error;
};
#endif

enum class EflSignPriorityDecision {
    NoPreference,
    PreferTrial,
    PreferParent,
};

struct JointConstraintAnchorState {
    bool active = false;
    double cra_deg = std::numeric_limits<double>::quiet_NaN();
    double distortion = std::numeric_limits<double>::quiet_NaN();
};

struct JointConstraintAnchorDecision {
    bool block_cra = false;
    bool block_distortion = false;

    bool blocked() const {
        return block_cra || block_distortion;
    }
};

using ScoreBatchEvaluator = std::function<std::vector<Score>(
    const std::vector<double>& scaled_rows,
    int row_count,
    bool* used_cuda)>;

} }  // namespace optiland::optimizer_detail
