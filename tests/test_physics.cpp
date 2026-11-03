#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>

#include "sim/scenarios.hpp"
#include "sim/simulation.hpp"

using namespace nbody;

namespace {

// Equal masses on a circular orbit about their common centre: separation d, each at speed
// sqrt(G m / (2 d)), period pi * d * sqrt(2 d / (G m)).
Bodies circular_binary(float m, float d) {
  const float v = std::sqrt(m / (2.0f * d));
  Bodies b;
  b.push_back({-0.5f * d, 0, 0}, {0, -v, 0}, m);
  b.push_back({0.5f * d, 0, 0}, {0, v, 0}, m);
  return b;
}

}  // namespace

TEST_CASE("leapfrog closes a circular two-body orbit and conserves energy") {
  const float m = 1.0f, d = 2.0f;
  const double period = std::numbers::pi * d * std::sqrt(2.0 * d / m);
  Simulation sim(circular_binary(m, d), {.dt = 0.001f, .softening = 1e-4f});
  const double e0 = sim.diagnostics().total_energy();

  const int steps = int(std::lround(period / 0.001));
  for (int i = 0; i < steps; ++i) sim.step();

  const glm::vec3 p0 = sim.bodies().position(0);
  CHECK(glm::distance(p0, glm::vec3(-0.5f * d, 0, 0)) < 0.01f);
  const double drift = std::abs((sim.diagnostics().total_energy() - e0) / e0);
  CHECK(drift < 1e-5);
}

TEST_CASE("energy error stays bounded over many orbits") {
  // A symplectic integrator's error oscillates rather than accumulating.
  Simulation sim(circular_binary(1.0f, 2.0f), {.dt = 0.01f, .softening = 1e-4f});
  const double e0 = sim.diagnostics().total_energy();
  double worst = 0.0;
  for (int i = 0; i < 20000; ++i) {  // ~16 orbits
    sim.step();
    if (i % 100 == 0) worst = std::max(worst, std::abs((sim.diagnostics().total_energy() - e0) / e0));
  }
  CHECK(worst < 1e-3);
}

TEST_CASE("momentum is conserved in a self-gravitating cluster") {
  ScenarioSetup s = make_scenario({.kind = Scenario::PlummerSphere, .body_count = 512, .seed = 7});
  Simulation sim(std::move(s.bodies), s.params);
  const Diagnostics d0 = sim.diagnostics();
  CHECK(glm::length(d0.momentum) < 1e-3);  // scenarios start in the centre-of-mass frame
  for (int i = 0; i < 200; ++i) sim.step();
  const Diagnostics d1 = sim.diagnostics();
  // Newton's third law holds pairwise up to float rounding in the summation order.
  CHECK(glm::length(d1.momentum - d0.momentum) < 1e-2);
  CHECK(glm::length(d1.angular_momentum - d0.angular_momentum) < 0.5);
}

TEST_CASE("Plummer sphere starts near virial equilibrium") {
  ScenarioSetup s = make_scenario({.kind = Scenario::PlummerSphere, .body_count = 4096, .seed = 1});
  const Diagnostics d = compute_diagnostics(s.bodies, s.params.G, s.params.softening);
  const double virial = 2.0 * d.kinetic / std::abs(d.potential);
  CHECK(virial == doctest::Approx(1.0).epsilon(0.1));
}

TEST_CASE("scenarios produce the requested body count") {
  for (std::size_t k = 0; k < kScenarioIds.size(); ++k) {
    ScenarioSetup s = make_scenario({.kind = Scenario(k), .body_count = 1000});
    CHECK(s.bodies.size() == 1000);
    CHECK(s.params.softening > 0.0f);
    CHECK(scenario_from_id(kScenarioIds[k]) == Scenario(k));
  }
}

TEST_CASE("changing softening refreshes accelerations") {
  Bodies b;
  b.push_back({0, 0, 0}, {0, 0, 0}, 1.0f);
  b.push_back({1, 0, 0}, {0, 0, 0}, 1.0f);
  Simulation sim(b, {.dt = 0.01f, .softening = 0.01f});
  const float before = sim.bodies().ax[0];
  sim.set_params({.dt = 0.01f, .softening = 1.0f});
  CHECK(sim.bodies().ax[0] < before);
  CHECK(sim.bodies().ax[0] == doctest::Approx(1.0 / std::pow(2.0, 1.5)).epsilon(1e-4));
}
