#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

double seconds_since(Clock::time_point start);

std::string research_snapshot_path(
    const std::string& directory,
    const std::string& filename);

std::string research_json_escape(const std::string& value);

void research_write_json_number(std::ostream& out, double value);

} }  // namespace optiland::optimizer_detail
