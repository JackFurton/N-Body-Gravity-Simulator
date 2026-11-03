#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include "render/camera.hpp"

using namespace nbody;

TEST_CASE("default camera looks down -z") {
  FlyCamera cam;
  const glm::vec3 f = cam.forward();
  CHECK(f.z == doctest::Approx(-1.0f));
  CHECK(glm::length(cam.right() - glm::vec3(1, 0, 0)) < 1e-5f);
}

TEST_CASE("pitch is clamped short of the poles") {
  FlyCamera cam;
  cam.update({.look = {0.0f, -1e6f}}, 0.016f);
  CHECK(cam.forward().y < 1.0f);
  CHECK(cam.forward().y > 0.99f);
}

TEST_CASE("frame puts the origin in front of the camera") {
  FlyCamera cam;
  cam.frame(20.0f);
  const glm::vec3 to_origin = glm::normalize(-cam.position);
  CHECK(glm::dot(to_origin, cam.forward()) == doctest::Approx(1.0f));
}
