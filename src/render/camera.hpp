#pragma once

#include <glm/gtc/constants.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace nbody {

struct CameraInput {
  glm::vec3 move{0.0f};  // x = right, y = up, z = forward, each in [-1, 1]
  glm::vec2 look{0.0f};  // cursor delta in screen points
  float scroll = 0.0f;   // wheel ticks, scales fly speed
  bool boost = false;
};

class FlyCamera {
 public:
  glm::vec3 position{0.0f, 0.0f, 40.0f};
  float yaw = -glm::half_pi<float>();  // radians; -pi/2 looks down -z
  float pitch = 0.0f;
  float speed = 10.0f;  // world units per second
  float fov_y = glm::radians(60.0f);
  float look_sensitivity = 0.0025f;

  glm::vec3 forward() const;
  glm::vec3 right() const;

  void update(const CameraInput& input, float dt);
  // Back off along a fixed diagonal until a sphere of `radius` around the origin fills the view.
  void frame(float radius);

  glm::mat4 view() const;
  glm::mat4 projection(float aspect) const;
};

}  // namespace nbody
