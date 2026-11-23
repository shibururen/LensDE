#pragma once

namespace optiland {
enum class AdaptiveDeVariant {
    ClassicBest1Bin,
    ClassicRand1Bin,
    ClassicCurrentToBest1Bin,
    JdeRand1Bin,
    JadePBestArchive,
    JadePBestArchiveLpsr,
    JadePBestArchiveLpsrFeas,
};

enum class PBestGuideScope {
    // Preserve Optiland's historical topology-guided behavior: 70% same
    // diversity key when available, otherwise/global top-pbest fallback.
    Legacy,
    // Standard JADE: draw pbest directly from the global top-p% population.
    Global,
    // Only use population pbest candidates that share the current diversity key.
    // If no same-key candidate exists, fall back to a no-pbest/current-to-rand step.
    SameKey,
    // Prefer basin-archive elites from the same/nearest basin, then same-key population
    // pbest, and otherwise no-pbest/current-to-rand.
    BasinLocal,
    // Probabilistic mixture controlled by pbest_*_rate fields.
    Mixed,
    // Disable pbest guidance for JADE-like variants; use current-to-rand fallback.
    None,
};

enum class DeMatingScope {
    // Preserve current behavior: draw DE difference vectors from the global pool.
    Global,
    // Draw DE difference vectors from rows with the same diversity key when possible.
    SameKey,
    // Draw DE difference vectors from the local basin/similarity family when possible.
    BasinLocal,
    // Use basin-local mating only for targets in the currently active basin families.
    ActiveBasinOnly,
    // Use local-basin mating with probability de_mating_local_rate, global otherwise.
    Mixed,
};

enum class ConstraintHandlingMode {
    ScalarMerit,
    FeasibilityRules,
    EpsilonConstraint,
};

enum class TopologyDiversityMode {
    Legacy,
    DescriptorShadow,
    DescriptorAudit,
    DescriptorSelect,
    DescriptorFull,
    TopologySimilarity,
    TopologyFamilySimilarity,
};


}  // namespace optiland
