// Order the command preparation, initial evaluation, optimization, and publication stages.
// OptimizationCommand is created only when the selected mode requests DE or local optimization.
// Propagate each stage status immediately so failed preparation cannot publish a successful result.

#include "command_context.hpp"
#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

int CommandContext::execute() {
    if (const int status = dispatch_queries(); status != kContinueCommand) return status;
    if (const int status = load_system(); status != kContinueCommand) return status;
    if (const int status = configure_calibrated_aiming(); status != kContinueCommand) return status;
    if (const int status = configure_three_step_aiming(); status != kContinueCommand) return status;
    if (const int status = configure_aperture_contract(); status != kContinueCommand) return status;
    if (const int status = load_merit(); status != kContinueCommand) return status;
    if (const int status = override_merit(); status != kContinueCommand) return status;
    if (const int status = prepare_vignetting(); status != kContinueCommand) return status;
    if (const int status = configure_execution(); status != kContinueCommand) return status;
    if (const int status = prepare_output(); status != kContinueCommand) return status;
    if (const int status = select_seed_population(); status != kContinueCommand) return status;
    if (const int status = configure_variables(); status != kContinueCommand) return status;
    if (const int status = evaluate_initial_system(); status != kContinueCommand) return status;
    if (const int status = optimize(); status != kContinueCommand) return status;
    if (const int status = publish(); status != kContinueCommand) return status;
    return 0;
}

int CommandContext::optimize() {
    if (run_de || run_local) {
        OptimizationCommand optimization(*this);
        return optimization.execute();
    }
    return kContinueCommand;
}

} } }
