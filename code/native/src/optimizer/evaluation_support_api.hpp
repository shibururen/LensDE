#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

LensSystem prepare_for_merit(LensSystem lens, const MeritConfig& config);

} }  // namespace optiland::optimizer_detail
