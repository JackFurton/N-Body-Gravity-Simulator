#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "sim/bodies.hpp"
#include "sim/simulation.hpp"

namespace nbody {

enum class Scenario : std::uint8_t { DiskGalaxy, GalaxyCollision, PlummerSphere, ColdCollapse };

inline constexpr std::array<std::string_view, 4> kScenarioNames = {
    "Disk galaxy", "Galaxy collision", "Plummer sphere", "Cold collapse"};
inline constexpr std::array<std::string_view, 4> kScenarioIds = {"disk", "collision", "plummer", "collapse"};

std::optional<Scenario> scenario_from_id(std::string_view id);

struct ScenarioConfig {
  Scenario kind = Scenario::DiskGalaxy;
  std::size_t body_count = 4096;
  std::uint32_t seed = 42;
};

struct ScenarioSetup {
  Bodies bodies;
  SimParams params;
  float view_radius;  // rough extent, for framing the camera
};

// Every scenario is returned in its centre-of-mass frame, so the cloud does not wander off screen.
ScenarioSetup make_scenario(const ScenarioConfig& config);

}  // namespace nbody
