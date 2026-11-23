/*
 * Sequence the complete adaptive session from runtime setup to final result construction.
 * The public optimizer entry creates this session; every called phase reads or initializes its shared run_.
 * Keep initialization and execution order explicit, including diagnostic and geometry-memory setup.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {
OptimizationResult AdaptiveSession::execute() {
    this->configure_runtime();

    this->initialize_topology_avoidance();

    this->initialize_population_evaluation();

    this->initialize_basin_archive();

    this->initialize_basin_lifecycle();

    this->initialize_search_dynamics();

    this->initialize_convergence();

    this->initialize_diagnostics();

    this->initialize_geometry_memory_mutation();

    this->initialize_active_focus();

    this->run_search_epochs();

    return this->finish_result();
}
} }
