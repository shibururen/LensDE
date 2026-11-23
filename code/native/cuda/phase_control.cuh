#pragma once
namespace optiland::native_detail {
enum class PhaseFlow { Proceed, Return, Continue, Break };
struct PhaseOutcome {
    PhaseFlow flow;
    bool value;
};
} // namespace optiland::native_detail
