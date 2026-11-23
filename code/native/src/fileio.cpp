/*
 * Public merit-JSON loader used by native CLI configuration before optical scoring.
 * Delegates to the io_detail settings readers in the established family order,
 * preserving caller defaults, compatibility aliases and exception-to-error reporting.
 */

#include "io/detail.hpp"

namespace optiland {
using namespace io_detail;

bool load_merit_json(const std::string& path, MeritConfig* config, std::string* error) {
    if (!config) {
        return false;
    }
    try {
        Json root = JsonParser(read_text_file(path)).parse();
        read_merit_targets(root, config);
        read_merit_manufacturing(root, config);
        read_merit_quality(root, config);
        read_merit_vignetting(root, config);
        read_merit_chromatic_quality(root, config);
        read_merit_sampling(root, config);
        read_merit_trace(root, config);
        read_merit_image_weights(root, config);
        read_merit_constraints(root, config);
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

}  // namespace optiland
