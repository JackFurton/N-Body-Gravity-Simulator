#include "sim/simulation.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

#include <glm/geometric.hpp>

namespace nbody {

void compute_accelerations_direct(Bodies& b, const SimParams& p) {
  const ForceInputs in{b.px.data(), b.py.data(), b.pz.data(), b.mass.data(), b.size(), p.G, p.softening};
  compute_direct_forces(p.kernel, in, {b.ax.data(), b.ay.data(), b.az.data()}, 0, b.size());
}

Diagnostics compute_diagnostics(const Bodies& b, float G, float softening) {
  Diagnostics d;
  const std::size_t n = b.size();
  const double eps2 = double(softening) * softening;
  for (std::size_t i = 0; i < n; ++i) {
    const glm::dvec3 p = b.position(i);
    const glm::dvec3 v = b.velocity(i);
    const double m = b.mass[i];
    d.kinetic += 0.5 * m * glm::dot(v, v);
    d.momentum += m * v;
    d.angular_momentum += m * glm::cross(p, v);

    double phi = 0.0;
    for (std::size_t j = i + 1; j < n; ++j) {
      const glm::dvec3 r = glm::dvec3(b.position(j)) - p;
      phi += b.mass[j] / std::sqrt(glm::dot(r, r) + eps2);
    }
    d.potential -= G * m * phi;
  }
  return d;
}

Simulation::Simulation(Bodies bodies, SimParams params) : bodies_(std::move(bodies)), params_(params) {
  if (!(params_.softening > 0.0f)) throw std::invalid_argument("softening must be positive");
  compute_accelerations_direct(bodies_, params_);
}

void Simulation::set_params(const SimParams& params) {
  if (!(params.softening > 0.0f)) throw std::invalid_argument("softening must be positive");
  const bool forces_changed =
      params.softening != params_.softening || params.G != params_.G || params.kernel != params_.kernel;
  params_ = params;
  // dt can change freely between KDK steps, but the stored accelerations belong to the old force law.
  if (forces_changed) compute_accelerations_direct(bodies_, params_);
}

void Simulation::step() {
  auto& b = bodies_;
  const std::size_t n = b.size();
  const float dt = params_.dt;
  const float half = 0.5f * dt;

  for (std::size_t i = 0; i < n; ++i) {
    b.vx[i] += b.ax[i] * half;
    b.vy[i] += b.ay[i] * half;
    b.vz[i] += b.az[i] * half;
    b.px[i] += b.vx[i] * dt;
    b.py[i] += b.vy[i] * dt;
    b.pz[i] += b.vz[i] * dt;
  }

  compute_accelerations_direct(b, params_);

  for (std::size_t i = 0; i < n; ++i) {
    b.vx[i] += b.ax[i] * half;
    b.vy[i] += b.ay[i] * half;
    b.vz[i] += b.az[i] * half;
  }

  time_ += dt;
  ++steps_;
}

}  // namespace nbody
