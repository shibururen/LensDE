#pragma once
// Private implementation prerequisites; compile through the owning .cu file.
#include "optiland/evaluation_certificate.hpp"
#include <mutex>
#include "optiland/spectral_aperture.hpp"
#include "optiland/fixed_budget_sampling.hpp"
#include "optiland/optimizer.hpp"
#include "optiland/cuda_launch_config.hpp"
#include "optiland/raytrace.hpp"
#include "../de_cuda_device_common.cuh"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "../phase_control.cuh"
