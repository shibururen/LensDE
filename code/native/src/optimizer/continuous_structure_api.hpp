#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

void append_continuous_values(
    std::ostringstream& out,
    char field,
    const std::vector<double>& values);

std::string optical_continuous_structure_signature(
    const LensSystem& system,
    double target_efl);

bool parse_continuous_values(
    const std::string& text,
    std::vector<double>* values);

bool parse_continuous_structure_signature(
    const std::string& key,
    ContinuousStructureSignature* signature);

double continuous_vector_norm(const std::vector<double>& values);

double continuous_vector_cosine(
    const std::vector<double>& left,
    const std::vector<double>& right);

double continuous_vector_normalized_distance(
    const std::vector<double>& left,
    const std::vector<double>& right);

bool continuous_sign_skeleton_compatible(
    const std::vector<double>& left,
    const std::vector<double>& right,
    double significance_fraction);

double continuous_distance_similarity(
    const std::vector<double>& left,
    const std::vector<double>& right);

ContinuousAvoidMatch continuous_parameter_auxiliary_match(
    const ContinuousStructureSignature& left,
    const ContinuousStructureSignature& right,
    const AdaptiveHybridDeDlsOptions& options);

} }  // namespace optiland::optimizer_detail
