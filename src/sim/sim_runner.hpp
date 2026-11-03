#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

#include <glm/vec4.hpp>

#include "sim/scenarios.hpp"
#include "sim/simulation.hpp"
#include "sim/triple_buffer.hpp"

namespace nbody {

struct Snapshot {
  std::vector<glm::vec4> bodies;  // xyz = position, w = speed
  float rms_speed = 0.0f;
  double sim_time = 0.0;
  std::uint64_t step = 0;
  std::uint64_t generation = 0;  // bumps on every reset
  float view_radius = 0.0f;
};

struct RunnerSettings {
  bool paused = false;
  float target_steps_per_sec = 120.0f;
  SimParams params;
};

struct RunnerStats {
  std::size_t bodies = 0;
  double sim_time = 0.0;
  std::uint64_t steps = 0;
  double step_ms = 0.0;        // smoothed wall time of one step
  double steps_per_sec = 0.0;  // measured
  std::optional<Diagnostics> initial;
  std::optional<Diagnostics> current;
};

// Owns the Simulation and advances it on a dedicated thread at a capped step rate, so a slow
// render frame never slows physics and a slow step never stalls the UI.
class SimRunner {
 public:
  explicit SimRunner(const ScenarioConfig& initial);
  SimRunner(const SimRunner&) = delete;
  SimRunner& operator=(const SimRunner&) = delete;

  void set_paused(bool paused);
  void set_target_rate(float steps_per_sec);
  void set_params(const SimParams& params);
  void reset(const ScenarioConfig& config);
  void step_once();

  RunnerSettings settings() const;
  RunnerStats stats() const;

  // Render thread only. The reference stays valid until the next call.
  const Snapshot& latest();

 private:
  void run(std::stop_token stop, ScenarioConfig initial);
  void publish(const Simulation& sim, float view_radius);

  mutable std::mutex mutex_;
  std::condition_variable_any wake_;
  RunnerSettings settings_;
  RunnerStats stats_;
  std::optional<ScenarioConfig> pending_reset_;
  int pending_steps_ = 0;

  TripleBuffer<Snapshot> snapshots_;
  std::uint64_t generation_ = 0;  // sim thread only

  std::jthread thread_;  // last: starts after every other member exists, joins before they go
};

}  // namespace nbody
