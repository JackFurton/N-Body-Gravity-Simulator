#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include <glm/geometric.hpp>

#include "sim/force_kernels.hpp"
#include "sim/scenarios.hpp"

using namespace nbody;

namespace {

struct Forces {
  std::vector<float> ax, ay, az;
  explicit Forces(std::size_t n) : ax(n, -1.0f), ay(n, -1.0f), az(n, -1.0f) {}
  ForceOutputs out() { return {ax.data(), ay.data(), az.data()}; }
};

ForceInputs inputs_of(const ScenarioSetup& s) {
  const Bodies& b = s.bodies;
  return {b.px.data(), b.py.data(), b.pz.data(), b.mass.data(), b.size(), 1.0f, s.params.softening};
}

double max_relative_error(const std::vector<glm::dvec3>& ref, const Forces& f) {
  double worst = 0.0;
  for (std::size_t i = 0; i < ref.size(); ++i) {
    const glm::dvec3 got{f.ax[i], f.ay[i], f.az[i]};
    worst = std::max(worst, glm::length(got - ref[i]) / glm::length(ref[i]));
  }
  return worst;
}

}  // namespace

TEST_CASE("every kernel matches the double-precision reference") {
  // 1003 is deliberately not a multiple of any block width, so the scalar tail runs too.
  for (const std::size_t n : {std::size_t(7), std::size_t(64), std::size_t(1003)}) {
    const ScenarioSetup s = make_scenario({.kind = Scenario::PlummerSphere, .body_count = n, .seed = 11});
    const ForceInputs in = inputs_of(s);
    const auto ref = reference_direct_forces(in);
    for (std::size_t k = 0; k < kForceKernelNames.size(); ++k) {
      CAPTURE(n);
      CAPTURE(kForceKernelNames[k]);
      Forces f(n);
      compute_direct_forces(ForceKernel(k), in, f.out(), 0, n);
      CHECK(max_relative_error(ref, f) < 2e-5);
    }
  }
}

TEST_CASE("splitting the target range gives the same answer as one call") {
  const std::size_t n = 997;
  const ScenarioSetup s = make_scenario({.kind = Scenario::DiskGalaxy, .body_count = n, .seed = 5});
  const ForceInputs in = inputs_of(s);
  for (std::size_t k = 0; k < kForceKernelNames.size(); ++k) {
    CAPTURE(kForceKernelNames[k]);
    Forces whole(n), split(n);
    compute_direct_forces(ForceKernel(k), in, whole.out(), 0, n);
    // Unaligned chunk boundaries, as a thread pool handing out uneven ranges would produce.
    for (std::size_t begin = 0; begin < n;) {
      const std::size_t end = std::min(n, begin + 37 + begin % 13);
      compute_direct_forces(ForceKernel(k), in, split.out(), begin, end);
      begin = end;
    }
    CHECK(whole.ax == split.ax);
    CHECK(whole.ay == split.ay);
    CHECK(whole.az == split.az);
  }
}
