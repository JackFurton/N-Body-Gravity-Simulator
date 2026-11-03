#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include "sim/bodies.hpp"

namespace nbody {

struct SimParams {
  float dt = 0.004f;
  float softening = 0.05f;
  float G = 1.0f;

  bool operator==(const SimParams&) const = default;
};

struct Diagnostics {
  double kinetic = 0.0;
  double potential = 0.0;
  glm::dvec3 momentum{0.0};
  glm::dvec3 angular_momentum{0.0};

  double total_energy() const { return kinetic + potential; }
};

// Softened direct summation, O(n^2). Writes ax/ay/az.
void compute_accelerations_direct(Bodies& bodies, float G, float softening);

// Kinetic and (softened) potential energy plus momenta, accumulated in double. O(n^2).
Diagnostics compute_diagnostics(const Bodies& bodies, float G, float softening);

class Simulation {
 public:
  Simulation(Bodies bodies, SimParams params);

  // One kick-drift-kick leapfrog step. Symplectic, so energy error stays bounded instead of
  // drifting the way it does with explicit Euler.
  void step();

  void set_params(const SimParams& params);

  const Bodies& bodies() const { return bodies_; }
  const SimParams& params() const { return params_; }
  double time() const { return time_; }
  std::uint64_t step_count() const { return steps_; }
  Diagnostics diagnostics() const { return compute_diagnostics(bodies_, params_.G, params_.softening); }

 private:
  Bodies bodies_;
  SimParams params_;
  double time_ = 0.0;
  std::uint64_t steps_ = 0;
};

}  // namespace nbody
