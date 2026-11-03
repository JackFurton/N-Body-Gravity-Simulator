#include "sim/scenarios.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <random>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat3x3.hpp>

namespace nbody {
namespace {

using Rng = std::mt19937;

glm::vec3 random_unit_vector(Rng& rng) {
  std::uniform_real_distribution<float> u(-1.0f, 1.0f);
  std::uniform_real_distribution<float> phi_dist(0.0f, 2.0f * std::numbers::pi_v<float>);
  const float z = u(rng);
  const float phi = phi_dist(rng);
  const float s = std::sqrt(std::max(0.0f, 1.0f - z * z));
  return {s * std::cos(phi), s * std::sin(phi), z};
}

struct DiskSpec {
  std::size_t stars;
  float center_mass;
  float disk_mass;
  float r_min, r_max, scale_length, thickness;
  float dispersion;  // fraction of circular speed
  glm::mat3 orientation{1.0f};
  glm::vec3 offset{0.0f};
  glm::vec3 bulk_velocity{0.0f};
};

// Exponential disk orbiting a central point mass. Circular speeds use the spherically enclosed
// mass, which is not an exact equilibrium for a flat disk; the mismatch seeds spiral structure.
void add_disk(Bodies& out, const DiskSpec& spec, Rng& rng) {
  std::gamma_distribution<float> radius_dist(2.0f, spec.scale_length);  // r * exp(-r / Rd)
  std::uniform_real_distribution<float> angle(0.0f, 2.0f * std::numbers::pi_v<float>);
  std::normal_distribution<float> gauss(0.0f, 1.0f);

  std::vector<float> radii(spec.stars);
  for (auto& r : radii) {
    do r = radius_dist(rng);
    while (r < spec.r_min || r > spec.r_max);
  }
  std::sort(radii.begin(), radii.end());

  out.push_back(spec.offset, spec.bulk_velocity, spec.center_mass);
  const float star_mass = spec.disk_mass / float(spec.stars);
  for (std::size_t k = 0; k < spec.stars; ++k) {
    const float r = radii[k];
    const float enclosed = spec.center_mass + spec.disk_mass * (float(k) / float(spec.stars));
    const float v_circ = std::sqrt(enclosed / r);
    const float theta = angle(rng);
    const glm::vec3 radial{std::cos(theta), 0.0f, std::sin(theta)};
    const glm::vec3 tangent{-std::sin(theta), 0.0f, std::cos(theta)};
    const glm::vec3 p = radial * r + glm::vec3(0.0f, gauss(rng) * spec.thickness, 0.0f);
    const glm::vec3 v = tangent * v_circ + glm::vec3(gauss(rng), gauss(rng) * 0.5f, gauss(rng)) *
                                               (spec.dispersion * v_circ);
    out.push_back(spec.offset + spec.orientation * p, spec.bulk_velocity + spec.orientation * v, star_mass);
  }
}

// Aarseth, Henon & Wielen (1974) sampling, G = 1.
void add_plummer(Bodies& out, std::size_t n, float total_mass, float a, Rng& rng) {
  std::uniform_real_distribution<float> u(0.0f, 1.0f);
  const float m = total_mass / float(n);
  const float v_scale = std::sqrt(total_mass / a);
  for (std::size_t i = 0; i < n; ++i) {
    float r;
    do r = a / std::sqrt(std::pow(std::max(u(rng), 1e-6f), -2.0f / 3.0f) - 1.0f);
    while (r > 12.0f * a);

    float q, g;
    do {
      q = u(rng);
      g = q * q * std::pow(1.0f - q * q, 3.5f);
    } while (0.1f * u(rng) > g);
    const float v = q * std::sqrt(2.0f) * std::pow(1.0f + r * r / (a * a), -0.25f) * v_scale;

    out.push_back(random_unit_vector(rng) * r, random_unit_vector(rng) * v, m);
  }
}

void add_uniform_sphere(Bodies& out, std::size_t n, float total_mass, float radius, float spin, Rng& rng) {
  std::uniform_real_distribution<float> u(0.0f, 1.0f);
  const float m = total_mass / float(n);
  for (std::size_t i = 0; i < n; ++i) {
    const glm::vec3 p = random_unit_vector(rng) * (radius * std::cbrt(u(rng)));
    // Solid-body rotation about +y, so the collapse settles into a flattened, rotating remnant.
    const glm::vec3 v = glm::cross(glm::vec3(0.0f, spin, 0.0f), p);
    out.push_back(p, v, m);
  }
}

void to_center_of_mass_frame(Bodies& b) {
  double mt = 0.0;
  glm::dvec3 com{0.0}, vcom{0.0};
  for (std::size_t i = 0; i < b.size(); ++i) {
    mt += b.mass[i];
    com += double(b.mass[i]) * glm::dvec3(b.position(i));
    vcom += double(b.mass[i]) * glm::dvec3(b.velocity(i));
  }
  const glm::vec3 c = com / mt, vc = vcom / mt;
  for (std::size_t i = 0; i < b.size(); ++i) {
    b.px[i] -= c.x, b.py[i] -= c.y, b.pz[i] -= c.z;
    b.vx[i] -= vc.x, b.vy[i] -= vc.y, b.vz[i] -= vc.z;
  }
}

}  // namespace

std::optional<Scenario> scenario_from_id(std::string_view id) {
  for (std::size_t i = 0; i < kScenarioIds.size(); ++i)
    if (kScenarioIds[i] == id) return Scenario(i);
  return std::nullopt;
}

ScenarioSetup make_scenario(const ScenarioConfig& config) {
  Rng rng(config.seed);
  const std::size_t n = std::max<std::size_t>(config.body_count, 2);
  ScenarioSetup s{};
  s.bodies.reserve(n);

  switch (config.kind) {
    case Scenario::DiskGalaxy:
      add_disk(s.bodies, {.stars = n - 1, .center_mass = 200.0f, .disk_mass = 60.0f, .r_min = 1.5f,
                          .r_max = 20.0f, .scale_length = 5.0f, .thickness = 0.25f, .dispersion = 0.04f},
               rng);
      s.params = {.dt = 0.004f, .softening = 0.08f};
      s.view_radius = 22.0f;
      break;

    case Scenario::GalaxyCollision: {
      // Roughly parabolic encounter with a 12-unit impact parameter; the second disk is tilted
      // 60 degrees so the tidal tails separate in 3D.
      const std::size_t half = n / 2;
      const glm::mat3 tilt = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(60.0f), {1.0f, 0.0f, 0.0f}));
      add_disk(s.bodies, {.stars = half - 1, .center_mass = 100.0f, .disk_mass = 30.0f, .r_min = 1.2f,
                          .r_max = 12.0f, .scale_length = 3.5f, .thickness = 0.2f, .dispersion = 0.04f,
                          .offset = {-25.0f, -6.0f, 0.0f}, .bulk_velocity = {1.6f, 0.0f, 0.0f}},
               rng);
      add_disk(s.bodies, {.stars = n - half - 1, .center_mass = 100.0f, .disk_mass = 30.0f, .r_min = 1.2f,
                          .r_max = 12.0f, .scale_length = 3.5f, .thickness = 0.2f, .dispersion = 0.04f,
                          .orientation = tilt, .offset = {25.0f, 6.0f, 0.0f}, .bulk_velocity = {-1.6f, 0.0f, 0.0f}},
               rng);
      s.params = {.dt = 0.004f, .softening = 0.08f};
      s.view_radius = 40.0f;
      break;
    }

    case Scenario::PlummerSphere:
      add_plummer(s.bodies, n, 100.0f, 5.0f, rng);
      s.params = {.dt = 0.004f, .softening = 0.1f};
      s.view_radius = 20.0f;
      break;

    case Scenario::ColdCollapse:
      add_uniform_sphere(s.bodies, n, 100.0f, 15.0f, 0.08f, rng);
      s.params = {.dt = 0.004f, .softening = 0.15f};
      s.view_radius = 18.0f;
      break;
  }

  to_center_of_mass_frame(s.bodies);
  return s;
}

}  // namespace nbody
