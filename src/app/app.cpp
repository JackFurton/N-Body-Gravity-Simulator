#include "app/app.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <utility>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

namespace nbody {
namespace {

GLFWwindow* create_window(int width, int height) {
  glfwSetErrorCallback([](int code, const char* msg) { std::fprintf(stderr, "GLFW error %d: %s\n", code, msg); });
  if (!glfwInit()) throw std::runtime_error("glfwInit failed");
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(width, height, "N-Body", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    throw std::runtime_error("glfwCreateWindow failed");
  }
  return window;
}

FrameData make_frame(const Snapshot& snap, const FlyCamera& camera, glm::ivec2 fb, const RenderSettings& settings) {
  return {
      .bodies = snap.bodies,
      .rms_speed = snap.rms_speed,
      .view = camera.view(),
      .proj = camera.projection(float(fb.x) / float(std::max(fb.y, 1))),
      .settings = settings,
  };
}

App* app_from(GLFWwindow* window) { return static_cast<App*>(glfwGetWindowUserPointer(window)); }

}  // namespace

App::App(const AppOptions& options)
    : options_(options),
      window_(create_window(options.width, options.height)),
      runner_(options.scenario),
      next_scenario_(options.scenario) {
  glfwSetWindowUserPointer(window_, this);
  // Installed before the ImGui backend so it chains to these rather than replacing them.
  glfwSetKeyCallback(window_, &App::on_key);
  glfwSetScrollCallback(window_, &App::on_scroll);
  renderer_ = std::make_unique<Renderer>(window_, options.capture_path.has_value());
  glfwSetWindowTitle(window_, ("N-Body  |  " + renderer_->device_name()).c_str());
}

App::~App() {
  renderer_.reset();
  glfwDestroyWindow(window_);
  glfwTerminate();
}

void App::on_key(GLFWwindow* window, int key, int, int action, int) {
  if (action != GLFW_PRESS || ImGui::GetIO().WantCaptureKeyboard) return;
  App& app = *app_from(window);
  switch (key) {
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
    case GLFW_KEY_SPACE: app.runner_.set_paused(!app.runner_.settings().paused); break;
    case GLFW_KEY_PERIOD: app.runner_.step_once(); break;
    case GLFW_KEY_R: app.runner_.reset(app.next_scenario_); break;
    case GLFW_KEY_F: app.framed_generation_ = 0; break;
    case GLFW_KEY_H:
    case GLFW_KEY_TAB: app.show_ui_ = !app.show_ui_; break;
    default: break;
  }
}

void App::on_scroll(GLFWwindow* window, double, double dy) {
  if (ImGui::GetIO().WantCaptureMouse) return;
  app_from(window)->pending_scroll_ += float(dy);
}

CameraInput App::poll_camera_input() {
  CameraInput in;
  const ImGuiIO& io = ImGui::GetIO();
  auto down = [&](int key) { return glfwGetKey(window_, key) == GLFW_PRESS; };

  if (!io.WantCaptureKeyboard) {
    in.move.z = float(down(GLFW_KEY_W)) - float(down(GLFW_KEY_S));
    in.move.x = float(down(GLFW_KEY_D)) - float(down(GLFW_KEY_A));
    in.move.y = float(down(GLFW_KEY_E)) - float(down(GLFW_KEY_Q));
    in.boost = down(GLFW_KEY_LEFT_SHIFT) || down(GLFW_KEY_RIGHT_SHIFT);
  }

  const bool button = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
                      glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  glm::dvec2 cursor;
  glfwGetCursorPos(window_, &cursor.x, &cursor.y);
  if (!looking_ && button && !io.WantCaptureMouse) {
    looking_ = true;
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwGetCursorPos(window_, &cursor.x, &cursor.y);
  } else if (looking_ && !button) {
    looking_ = false;
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  } else if (looking_) {
    in.look = glm::vec2(cursor - last_cursor_);
  }
  last_cursor_ = cursor;

  in.scroll = std::exchange(pending_scroll_, 0.0f);
  return in;
}

int App::run() {
  double last_time = glfwGetTime();
  while (!glfwWindowShouldClose(window_)) {
    glfwPollEvents();
    const double now = glfwGetTime();
    const float dt = float(std::min(now - last_time, 0.1));
    last_time = now;
    smoothed_frame_ms_ = smoothed_frame_ms_ == 0.0f ? dt * 1000.0f : 0.95f * smoothed_frame_ms_ + 0.05f * dt * 1000.0f;

    const Snapshot& snap = runner_.latest();
    if (snap.generation != framed_generation_ && snap.view_radius > 0.0f) {
      camera_.frame(snap.view_radius);
      framed_generation_ = snap.generation;
    }

    if (!renderer_->begin_frame()) {
      glfwWaitEventsTimeout(0.05);
      continue;
    }
    camera_.update(poll_camera_input(), dt);
    if (show_ui_) draw_ui();

    const FrameData frame = make_frame(snap, camera_, renderer_->framebuffer_size(), render_settings_);

    const bool capture = options_.capture_path && snap.generation > 0 && snap.step >= options_.capture_step;
    const bool ok = renderer_->end_frame(frame, capture ? options_.capture_path->c_str() : nullptr);
    if (capture) {
      if (!ok) {
        std::fprintf(stderr, "failed to write %s\n", options_.capture_path->c_str());
        return 1;
      }
      std::printf("wrote %s (step %llu, %zu bodies)\n", options_.capture_path->c_str(),
                  static_cast<unsigned long long>(snap.step), snap.bodies.size());
      return 0;
    }
  }
  return 0;
}

int run_headless(const AppOptions& options) {
  Renderer renderer(glm::ivec2(options.width, options.height));
  SimRunner runner(options.scenario);
  runner.set_target_rate(100000.0f);  // nobody is watching, run flat out

  // The runner drops intermediate snapshots, so poll until one at or past the target arrives.
  const Snapshot* snap = nullptr;
  while (true) {
    snap = &runner.latest();
    if (snap->generation > 0 && snap->step >= options.capture_step) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  runner.set_paused(true);

  FlyCamera camera;
  camera.frame(snap->view_radius);
  renderer.begin_frame();
  const FrameData frame = make_frame(*snap, camera, renderer.framebuffer_size(), RenderSettings{});
  if (!renderer.end_frame(frame, options.capture_path->c_str())) {
    std::fprintf(stderr, "failed to write %s\n", options.capture_path->c_str());
    return 1;
  }
  const RunnerStats stats = runner.stats();
  std::printf("wrote %s (%s, step %llu, t = %.2f, %zu bodies, %.2f ms/step)\n", options.capture_path->c_str(),
              renderer.device_name().c_str(), static_cast<unsigned long long>(snap->step), snap->sim_time,
              snap->bodies.size(), stats.step_ms);
  return 0;
}

void App::draw_ui() {
  const RunnerStats stats = runner_.stats();
  RunnerSettings settings = runner_.settings();

  ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({340, 0}, ImGuiCond_FirstUseEver);
  ImGui::Begin("N-Body", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

  ImGui::Text("%.0f fps  (%.2f ms)", 1000.0f / std::max(smoothed_frame_ms_, 1e-3f), smoothed_frame_ms_);

  if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
    const double n = double(stats.bodies);
    ImGui::Text("%zu bodies   t = %.2f   step %llu", stats.bodies, stats.sim_time,
                static_cast<unsigned long long>(stats.steps));
    ImGui::Text("%.2f ms/step   %.0f steps/s", stats.step_ms, stats.steps_per_sec);
    ImGui::Text("%.2f G interactions/s", n * n * stats.steps_per_sec * 1e-9);
    if (stats.initial && stats.current) {
      const double e0 = stats.initial->total_energy();
      const double drift = (stats.current->total_energy() - e0) / std::abs(e0);
      ImGui::Text("energy drift  %+.2e", drift);
      const double virial = 2.0 * stats.current->kinetic / std::abs(stats.current->potential);
      ImGui::Text("virial 2K/|W| %.3f", virial);
      ImGui::SetItemTooltip("1.0 means the system is in dynamical equilibrium");
    }

    bool params_changed = false;
    if (ImGui::Checkbox("Paused", &settings.paused)) runner_.set_paused(settings.paused);
    ImGui::SameLine();
    if (ImGui::Button("Step")) runner_.step_once();
    params_changed |= ImGui::SliderFloat("dt", &settings.params.dt, 1e-4f, 0.05f, "%.4f", ImGuiSliderFlags_Logarithmic);
    params_changed |= ImGui::SliderFloat("softening", &settings.params.softening, 0.005f, 2.0f, "%.3f",
                                         ImGuiSliderFlags_Logarithmic);
    if (params_changed) runner_.set_params(settings.params);
    if (ImGui::SliderFloat("max steps/s", &settings.target_steps_per_sec, 1.0f, 2000.0f, "%.0f",
                           ImGuiSliderFlags_Logarithmic))
      runner_.set_target_rate(settings.target_steps_per_sec);
  }

  if (ImGui::CollapsingHeader("Scenario", ImGuiTreeNodeFlags_DefaultOpen)) {
    int kind = int(next_scenario_.kind);
    if (ImGui::BeginCombo("preset", kScenarioNames[size_t(kind)].data())) {
      for (int i = 0; i < int(kScenarioNames.size()); ++i)
        if (ImGui::Selectable(kScenarioNames[size_t(i)].data(), i == kind)) next_scenario_.kind = Scenario(i);
      ImGui::EndCombo();
    }
    int count = int(next_scenario_.body_count);
    if (ImGui::SliderInt("bodies", &count, 256, 65536, "%d", ImGuiSliderFlags_Logarithmic))
      next_scenario_.body_count = size_t(count);
    int seed = int(next_scenario_.seed);
    if (ImGui::InputInt("seed", &seed)) next_scenario_.seed = std::uint32_t(seed);
    if (ImGui::Button("Reset (R)")) runner_.reset(next_scenario_);
    if (next_scenario_.body_count > 16384) {
      ImGui::SameLine();
      ImGui::TextDisabled("O(n^2): expect it to crawl");
    }
  }

  if (ImGui::CollapsingHeader("Render")) {
    ImGui::SliderFloat("star size", &render_settings_.star_size, 0.005f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("min pixels", &render_settings_.min_pixel_size, 0.5f, 6.0f, "%.1f");
    ImGui::SliderFloat("brightness", &render_settings_.brightness, 0.02f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("min flux", &render_settings_.min_flux, 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("colour range", &render_settings_.color_speed, 0.25f, 8.0f, "%.2f x rms",
                       ImGuiSliderFlags_Logarithmic);
  }

  if (ImGui::CollapsingHeader("Camera")) {
    ImGui::Text("pos  %.1f  %.1f  %.1f", camera_.position.x, camera_.position.y, camera_.position.z);
    ImGui::SliderFloat("fly speed", &camera_.speed, 0.01f, 1e4f, "%.2f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Reframe (F)")) framed_generation_ = 0;
  }

  ImGui::Separator();
  ImGui::TextDisabled("drag: look   WASD/QE: fly   shift: boost");
  ImGui::TextDisabled("scroll: fly speed   space: pause   .: step");
  ImGui::TextDisabled("R: reset   F: reframe   H: hide panel");
  ImGui::End();
}

}  // namespace nbody
