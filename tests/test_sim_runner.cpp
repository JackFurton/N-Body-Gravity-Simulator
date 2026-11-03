#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "sim/sim_runner.hpp"

using namespace nbody;
using namespace std::chrono_literals;

namespace {

template <class Pred>
const Snapshot& wait_for(SimRunner& runner, Pred pred) {
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (std::chrono::steady_clock::now() < deadline) {
    const Snapshot& s = runner.latest();
    if (pred(s)) return s;
    std::this_thread::sleep_for(1ms);
  }
  FAIL("timed out waiting for snapshot");
  return runner.latest();
}

}  // namespace

TEST_CASE("runner advances, pauses, single-steps and resets") {
  SimRunner runner({.kind = Scenario::PlummerSphere, .body_count = 256});
  runner.set_target_rate(5000.0f);

  const Snapshot& first = wait_for(runner, [](const Snapshot& s) { return s.step >= 20; });
  CHECK(first.generation == 1);
  CHECK(first.bodies.size() == 256);

  runner.set_paused(true);
  std::this_thread::sleep_for(20ms);
  const std::uint64_t paused_at = wait_for(runner, [](const Snapshot&) { return true; }).step;
  std::this_thread::sleep_for(30ms);
  CHECK(runner.latest().step == paused_at);

  runner.step_once();
  wait_for(runner, [&](const Snapshot& s) { return s.step == paused_at + 1; });

  runner.reset({.kind = Scenario::DiskGalaxy, .body_count = 300});
  const Snapshot& reset = wait_for(runner, [](const Snapshot& s) { return s.generation == 2; });
  CHECK(reset.bodies.size() == 300);
  CHECK(reset.step == 0);
  CHECK(runner.settings().paused);  // reset keeps the pause state
}

TEST_CASE("runner shuts down promptly while paused or pacing") {
  const auto start = std::chrono::steady_clock::now();
  {
    SimRunner slow({.kind = Scenario::PlummerSphere, .body_count = 64});
    slow.set_target_rate(1.0f);  // sleeping ~1 s between steps
    std::this_thread::sleep_for(20ms);
  }
  {
    SimRunner paused({.kind = Scenario::PlummerSphere, .body_count = 64});
    paused.set_paused(true);
    std::this_thread::sleep_for(20ms);
  }
  CHECK(std::chrono::steady_clock::now() - start < 500ms);
}
