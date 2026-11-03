#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>

namespace nbody {

struct ForceInputs {
  const float* px;
  const float* py;
  const float* pz;
  const float* mass;
  std::size_t n;
  float G;
  float softening;
};

struct ForceOutputs {
  float* ax;
  float* ay;
  float* az;
};

// Direct-summation variants. SimdX<B> keeps B vectors of 4 target bodies in registers and
// streams every source body past them. On M5 the plain sqrt + divide form beats a
// Newton-refined rsqrt estimate by ~50%, and blocking stops paying off at 4 (see nbody_bench).
enum class ForceKernel : std::uint8_t { Scalar, SimdRsqrtX4, SimdX1, SimdX2, SimdX4 };

inline constexpr std::array<std::string_view, 5> kForceKernelNames = {"scalar", "simd rsqrt x4", "simd x1",
                                                                      "simd x2", "simd x4"};
inline constexpr ForceKernel kDefaultForceKernel = ForceKernel::SimdX4;

// Writes accelerations for target bodies [i_begin, i_end) against all n sources. Ranges are
// independent, so callers may split one evaluation across threads.
void compute_direct_forces(ForceKernel kernel, const ForceInputs& in, const ForceOutputs& out, std::size_t i_begin,
                           std::size_t i_end);

// Same sum in double precision, for checking the float kernels. O(n^2) and slow.
std::vector<glm::dvec3> reference_direct_forces(const ForceInputs& in);

}  // namespace nbody
