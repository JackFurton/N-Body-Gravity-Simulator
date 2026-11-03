# N-Body Gravity Simulator

Real-time Newtonian gravity for thousands of bodies, with a Metal renderer you can fly around in.

C++23 sim core with no platform dependencies, physics on its own `std::jthread`, a lock-free triple buffer handing snapshots to an Objective-C++ Metal renderer, and Dear ImGui for controls. Roadmap lives in the [issues](https://github.com/JackFurton/N-Body-Gravity-Simulator/issues) and [milestones](https://github.com/JackFurton/N-Body-Gravity-Simulator/milestones).

## Build

Needs macOS on Apple silicon, the Xcode Command Line Tools, CMake 3.28+ and Ninja. Dependencies (GLFW, Dear ImGui, glm, doctest) are fetched at configure time.

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The `debug` preset builds with AddressSanitizer and UBSan.

## Run

```sh
./build/release/nbody                                  # disk galaxy, 4096 bodies
./build/release/nbody --scenario collision --bodies 8192
./build/release/nbody --headless --scenario collapse --capture-step 2000 --capture out.png
```

Scenarios: `disk`, `collision`, `plummer`, `collapse`. `--headless` renders offscreen without opening a window, which is what CI uses as a smoke test.

| Input | Action |
| --- | --- |
| drag (either button) | look |
| W A S D, Q E | fly, down / up |
| shift | 5x speed |
| scroll | change fly speed |
| space, `.` | pause, single step |
| R, F, H | reset scenario, reframe camera, hide panel |

## Physics

Kick-drift-kick leapfrog with Plummer softening, G = 1. Leapfrog is symplectic, so energy error oscillates instead of drifting; the panel shows the live drift and virial ratio, and the tests check a closed two-body orbit, momentum conservation and Plummer equilibrium. Forces are O(n^2) direct summation, about 5 G interactions/s on one M5 core. `nbody_bench` compares every kernel variant against a double-precision reference; the thread pool and Barnes-Hut come next.
