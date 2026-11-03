#include "sim/sim_runner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace nbody {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kDiagnosticsInterval = std::chrono::seconds(1);
}  // namespace

SimRunner::SimRunner(const ScenarioConfig& initial)
    : thread_([this, initial](std::stop_token stop) { run(stop, initial); }) {}

void SimRunner::set_paused(bool paused) {
  {
    std::scoped_lock lock(mutex_);
    settings_.paused = paused;
  }
  wake_.notify_all();
}

void SimRunner::set_target_rate(float steps_per_sec) {
  {
    std::scoped_lock lock(mutex_);
    settings_.target_steps_per_sec = std::clamp(steps_per_sec, 1.0f, 100000.0f);
  }
  wake_.notify_all();
}

void SimRunner::set_params(const SimParams& params) {
  std::scoped_lock lock(mutex_);
  settings_.params = params;
}

void SimRunner::reset(const ScenarioConfig& config) {
  {
    std::scoped_lock lock(mutex_);
    pending_reset_ = config;
  }
  wake_.notify_all();
}

void SimRunner::step_once() {
  {
    std::scoped_lock lock(mutex_);
    ++pending_steps_;
  }
  wake_.notify_all();
}

RunnerSettings SimRunner::settings() const {
  std::scoped_lock lock(mutex_);
  return settings_;
}

RunnerStats SimRunner::stats() const {
  std::scoped_lock lock(mutex_);
  return stats_;
}

const Snapshot& SimRunner::latest() {
  snapshots_.acquire();
  return snapshots_.front();
}

void SimRunner::publish(const Simulation& sim, float view_radius) {
  Snapshot& snap = snapshots_.back();
  const Bodies& b = sim.bodies();
  const std::size_t n = b.size();
  snap.bodies.resize(n);
  double speed2_sum = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const float s2 = b.vx[i] * b.vx[i] + b.vy[i] * b.vy[i] + b.vz[i] * b.vz[i];
    speed2_sum += s2;
    snap.bodies[i] = {b.px[i], b.py[i], b.pz[i], std::sqrt(s2)};
  }
  snap.rms_speed = n ? float(std::sqrt(speed2_sum / double(n))) : 0.0f;
  snap.sim_time = sim.time();
  snap.step = sim.step_count();
  snap.generation = generation_;
  snap.view_radius = view_radius;
  snapshots_.publish();
}

void SimRunner::run(std::stop_token stop, ScenarioConfig initial) {
  std::optional<ScenarioConfig> reset_to = initial;
  std::optional<Simulation> sim;
  float view_radius = 0.0f;

  auto next_step = Clock::now();
  auto next_diag = Clock::now();
  auto rate_window_start = Clock::now();
  std::uint64_t rate_window_steps = 0;

  while (!stop.stop_requested()) {
    if (reset_to) {
      ScenarioSetup setup = make_scenario(*reset_to);
      view_radius = setup.view_radius;
      sim.emplace(std::move(setup.bodies), setup.params);
      ++generation_;
      const Diagnostics d0 = sim->diagnostics();
      {
        std::scoped_lock lock(mutex_);
        settings_.params = setup.params;
        stats_ = RunnerStats{.bodies = sim->bodies().size(), .initial = d0, .current = d0};
      }
      publish(*sim, view_radius);
      reset_to.reset();
      next_step = next_diag = rate_window_start = Clock::now();
      rate_window_steps = 0;
    }

    RunnerSettings settings;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, stop, [&] { return !settings_.paused || pending_reset_ || pending_steps_ > 0; });
      if (stop.stop_requested()) break;
      if (pending_reset_) {
        reset_to = std::exchange(pending_reset_, std::nullopt);
        continue;
      }
      if (settings_.paused) --pending_steps_;
      else pending_steps_ = 0;
      settings = settings_;
    }

    sim->set_params(settings.params);
    const auto t0 = Clock::now();
    sim->step();
    const auto t1 = Clock::now();
    publish(*sim, view_radius);
    ++rate_window_steps;

    std::optional<Diagnostics> diag;
    if (t1 >= next_diag || settings.paused) {
      diag = sim->diagnostics();
      next_diag = Clock::now() + kDiagnosticsInterval;
    }

    {
      std::scoped_lock lock(mutex_);
      const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
      stats_.step_ms = stats_.step_ms == 0.0 ? ms : 0.9 * stats_.step_ms + 0.1 * ms;
      stats_.sim_time = sim->time();
      stats_.steps = sim->step_count();
      if (diag) stats_.current = diag;
      const double window = std::chrono::duration<double>(t1 - rate_window_start).count();
      if (window >= 0.5) {
        stats_.steps_per_sec = double(rate_window_steps) / window;
        rate_window_start = t1;
        rate_window_steps = 0;
      }
    }

    if (settings.paused) continue;

    // Fixed-rate pacing. If a step overran, re-anchor instead of sprinting to catch up.
    const auto period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / settings.target_steps_per_sec));
    next_step += period;
    const auto now = Clock::now();
    if (next_step < now) next_step = now;
    std::unique_lock lock(mutex_);
    wake_.wait_until(lock, stop, next_step, [&] { return pending_reset_.has_value(); });
  }
}

}  // namespace nbody
