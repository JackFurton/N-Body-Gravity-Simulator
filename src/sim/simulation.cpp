#include "sim/simulation.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

#include <glm/geometric.hpp>

namespace nbody {

void compute_accelerations_direct(Bodies& b, float G, float softening) {
  const std::size_t n = b.size();
  const float eps2 = softening * softening;
  const float* __restrict px = b.px.data();
  const float* __restrict py = b.py.data();
  const float* __restrict pz = b.pz.data();
  const float* __restrict m = b.mass.data();

  for (std::size_t i = 0; i < n; ++i) {
    const float xi = px[i], yi = py[i], zi = pz[i];
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    // j == i is included on purpose: dx = 0 contributes nothing once eps2 > 0, and keeping the
    // loop branch-free is what lets it vectorise.
    for (std::size_t j = 0; j < n; ++j) {
      const float dx = px[j] - xi;
      const float dy = py[j] - yi;
      const float dz = pz[j] - zi;
      const float r2 = dx * dx + dy * dy + dz * dz + eps2;
      const float inv_r = 1.0f / std::sqrt(r2);
      const float s = m[j] * inv_r * inv_r * inv_r;
      ax += dx * s;
      ay += dy * s;
      az += dz * s;
    }
    b.ax[i] = G * ax;
    b.ay[i] = G * ay;
    b.az[i] = G * az;
  }
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
  compute_accelerations_direct(bodies_, params_.G, params_.softening);
}

void Simulation::set_params(const SimParams& params) {
  if (!(params.softening > 0.0f)) throw std::invalid_argument("softening must be positive");
  const bool forces_changed = params.softening != params_.softening || params.G != params_.G;
  params_ = params;
  // dt can change freely between KDK steps, but the stored accelerations belong to the old force law.
  if (forces_changed) compute_accelerations_direct(bodies_, params_.G, params_.softening);
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

  compute_accelerations_direct(b, params_.G, params_.softening);

  for (std::size_t i = 0; i < n; ++i) {
    b.vx[i] += b.ax[i] * half;
    b.vy[i] += b.ay[i] * half;
    b.vz[i] += b.az[i] * half;
  }

  time_ += dt;
  ++steps_;
}

}  // namespace nbody
