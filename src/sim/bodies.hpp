#pragma once

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

namespace nbody {

// Structure-of-arrays so the force loop streams contiguous lanes of each component.
struct Bodies {
  std::vector<float> px, py, pz;
  std::vector<float> vx, vy, vz;
  std::vector<float> ax, ay, az;
  std::vector<float> mass;

  std::size_t size() const { return mass.size(); }

  void reserve(std::size_t n) {
    for (auto* v : {&px, &py, &pz, &vx, &vy, &vz, &ax, &ay, &az, &mass}) v->reserve(n);
  }

  void push_back(glm::vec3 p, glm::vec3 v, float m) {
    px.push_back(p.x), py.push_back(p.y), pz.push_back(p.z);
    vx.push_back(v.x), vy.push_back(v.y), vz.push_back(v.z);
    ax.push_back(0.0f), ay.push_back(0.0f), az.push_back(0.0f);
    mass.push_back(m);
  }

  glm::vec3 position(std::size_t i) const { return {px[i], py[i], pz[i]}; }
  glm::vec3 velocity(std::size_t i) const { return {vx[i], vy[i], vz[i]}; }
};

}  // namespace nbody
