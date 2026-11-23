/*
 * Loads the merit model, EFL/TTL/BFL/spot targets and basic weights/failure limits.
 * This first load_merit_json settings phase seeds the later family-specific readers.
 * Missing-key defaults and model aliases preserve the initial target configuration
 * before specialized quality, sampling and constraint options are applied.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
void read_merit_targets(const Json& root, MeritConfig* config) {
config->merit_model = root.at("merit_model").as_string(
            root.at("model").as_string(config->merit_model));
        config->target_efl = root.at("target_efl").as_number(config->target_efl);
        config->target_ttl = root.at("target_ttl").as_number(config->target_ttl);
        config->target_bfl = root.at("target_bfl").as_number(config->target_bfl);
        config->spot_target = root.at("spot_target").as_number(config->spot_target);
        config->weight_efl = root.at("weight_efl").as_number(config->weight_efl);
        config->weight_ttl = root.at("weight_ttl").as_number(config->weight_ttl);
        config->weight_bfl = root.at("weight_bfl").as_number(config->weight_bfl);
        config->weight_spot = root.at("weight_spot").as_number(config->weight_spot);
        config->weight_edge = root.at("weight_edge").as_number(config->weight_edge);
        config->fail_merit = root.at("fail_merit").as_number(config->fail_merit);
        config->min_edge_thickness = root.at("min_edge_thickness").as_number(config->min_edge_thickness);
}
}  // namespace io_detail

}  // namespace optiland
