#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <glm/vec2.hpp>

#include "render/camera.hpp"
#include "render/renderer.hpp"
#include "sim/scenarios.hpp"
#include "sim/sim_runner.hpp"

struct GLFWwindow;

namespace nbody {

struct AppOptions {
  ScenarioConfig scenario;
  int width = 1600;
  int height = 1000;
  std::optional<std::string> capture_path;  // write one PNG and exit
  std::uint64_t capture_step = 0;           // ...once the sim has reached this step
  bool headless = false;                    // no window or AppKit at all; requires capture_path
};

// Advances the sim to options.capture_step, renders one frame offscreen and writes the PNG.
int run_headless(const AppOptions& options);

class App {
 public:
  explicit App(const AppOptions& options);
  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  int run();

 private:
  static void on_key(GLFWwindow* window, int key, int scancode, int action, int mods);
  static void on_scroll(GLFWwindow* window, double dx, double dy);

  CameraInput poll_camera_input();
  void draw_ui();

  AppOptions options_;
  GLFWwindow* window_ = nullptr;
  std::unique_ptr<Renderer> renderer_;
  SimRunner runner_;
  FlyCamera camera_;
  RenderSettings render_settings_;
  ScenarioConfig next_scenario_;

  bool show_ui_ = true;
  bool looking_ = false;
  glm::dvec2 last_cursor_{0.0};
  float pending_scroll_ = 0.0f;
  std::uint64_t framed_generation_ = 0;
  float smoothed_frame_ms_ = 0.0f;
};

}  // namespace nbody
