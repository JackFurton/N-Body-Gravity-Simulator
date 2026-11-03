#include "render/camera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace nbody {
namespace {
constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
constexpr float kMaxPitch = 89.0f * glm::pi<float>() / 180.0f;
}  // namespace

glm::vec3 FlyCamera::forward() const {
  return {std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
}

glm::vec3 FlyCamera::right() const { return glm::normalize(glm::cross(forward(), kWorldUp)); }

void FlyCamera::update(const CameraInput& input, float dt) {
  yaw += input.look.x * look_sensitivity;
  pitch = std::clamp(pitch - input.look.y * look_sensitivity, -kMaxPitch, kMaxPitch);
  speed = std::clamp(speed * std::pow(1.2f, input.scroll), 0.01f, 1e5f);

  const glm::vec3 dir = right() * input.move.x + kWorldUp * input.move.y + forward() * input.move.z;
  if (glm::dot(dir, dir) > 0.0f)
    position += glm::normalize(dir) * speed * (input.boost ? 5.0f : 1.0f) * dt;
}

void FlyCamera::frame(float radius) {
  yaw = -glm::half_pi<float>();
  pitch = -glm::radians(30.0f);
  const float distance = radius / std::tan(0.5f * fov_y);
  position = -forward() * distance;
  speed = radius * 0.5f;
}

glm::mat4 FlyCamera::view() const { return glm::lookAt(position, position + forward(), kWorldUp); }

glm::mat4 FlyCamera::projection(float aspect) const {
  // Metal clip space is z in [0, 1]; GLM_FORCE_DEPTH_ZERO_TO_ONE is set for every target.
  return glm::perspective(fov_y, aspect, 0.01f, 1e5f);
}

}  // namespace nbody
