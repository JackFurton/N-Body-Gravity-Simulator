#pragma once

#include <memory>
#include <span>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

struct GLFWwindow;

namespace nbody {

struct RenderSettings {
  float star_size = 0.05f;
  float min_pixel_size = 2.0f;
  float brightness = 2.0f;
  float color_speed = 2.0f;  // multiples of the RMS speed that map to the hot end of the palette
  float min_flux = 0.15f;
};

struct FrameData {
  std::span<const glm::vec4> bodies;
  float rms_speed = 1.0f;
  glm::mat4 view{1.0f};
  glm::mat4 proj{1.0f};
  RenderSettings settings;
};

// Metal renderer and the ImGui platform/renderer backends. The implementation lives in an
// Objective-C++ file so nothing else in the project has to compile as Objective-C.
class Renderer {
 public:
  explicit Renderer(GLFWwindow* window, bool enable_capture = false);
  // Headless: renders into an offscreen texture of this size, no window, no ImGui. Only
  // end_frame's capture output is observable.
  explicit Renderer(glm::ivec2 offscreen_size);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // Acquires the next drawable and starts an ImGui frame. When it returns false (window
  // minimised or zero-sized) skip all ImGui calls and end_frame for this iteration.
  bool begin_frame();
  // Draws bodies and the ImGui draw data, then presents. If capture_path is set, the frame is
  // also written there as a PNG; returns false if that write failed.
  bool end_frame(const FrameData& frame, const char* capture_path = nullptr);

  glm::ivec2 framebuffer_size() const;
  std::string device_name() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace nbody
