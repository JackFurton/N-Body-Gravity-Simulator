#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

#include <glm/geometric.hpp>

#include "sim/force_kernels.hpp"
#include "sim/scenarios.hpp"

using namespace nbody;

namespace {

struct Options {
  std::vector<std::size_t> sizes{1024, 4096, 16384};
  int reps = 5;
  bool markdown = false;
};

std::vector<std::size_t> parse_sizes(std::string_view s) {
  std::vector<std::size_t> out;
  while (!s.empty()) {
    const auto comma = s.find(',');
    std::size_t n = 0;
    std::from_chars(s.data(), s.data() + std::min(comma, s.size()), n);
    if (n) out.push_back(n);
    s = comma == std::string_view::npos ? std::string_view{} : s.substr(comma + 1);
  }
  return out;
}

// Worst per-body error relative to that body's acceleration magnitude.
double max_relative_error(const std::vector<glm::dvec3>& ref, const ForceOutputs& out) {
  double worst = 0.0;
  for (std::size_t i = 0; i < ref.size(); ++i) {
    const glm::dvec3 got{out.ax[i], out.ay[i], out.az[i]};
    worst = std::max(worst, glm::length(got - ref[i]) / std::max(glm::length(ref[i]), 1e-30));
  }
  return worst;
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--sizes" && i + 1 < argc) opt.sizes = parse_sizes(argv[++i]);
    else if (a == "--reps" && i + 1 < argc) opt.reps = std::max(1, std::atoi(argv[++i]));
    else if (a == "--markdown") opt.markdown = true;
    else {
      std::fprintf(stderr, "usage: nbody_bench [--sizes 1024,4096] [--reps 5] [--markdown]\n");
      return 2;
    }
  }

  if (opt.markdown) std::puts("| kernel | N | ms / eval | G interactions/s | max rel err |\n|---|---:|---:|---:|---:|");
  else std::printf("%-14s %8s %11s %10s %12s\n", "kernel", "N", "ms/eval", "Gint/s", "max rel err");

  for (const std::size_t n : opt.sizes) {
    const ScenarioSetup s = make_scenario({.kind = Scenario::PlummerSphere, .body_count = n, .seed = 3});
    const Bodies& b = s.bodies;
    const ForceInputs in{b.px.data(), b.py.data(), b.pz.data(), b.mass.data(), n, 1.0f, s.params.softening};
    const std::vector<glm::dvec3> ref = reference_direct_forces(in);
    std::vector<float> ax(n), ay(n), az(n);
    const ForceOutputs out{ax.data(), ay.data(), az.data()};

    for (std::size_t k = 0; k < kForceKernelNames.size(); ++k) {
      const auto kernel = ForceKernel(k);
      compute_direct_forces(kernel, in, out, 0, n);  // warm-up, and the result we check
      const double err = max_relative_error(ref, out);
      double best = 1e30;
      for (int r = 0; r < opt.reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        compute_direct_forces(kernel, in, out, 0, n);
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
      }
      const double gint = double(n) * double(n) / best * 1e-9;
      if (opt.markdown)
        std::printf("| %s | %zu | %.3f | %.2f | %.1e |\n", kForceKernelNames[k].data(), n, best * 1e3, gint, err);
      else
        std::printf("%-14s %8zu %11.3f %10.2f %12.1e\n", kForceKernelNames[k].data(), n, best * 1e3, gint, err);
    }
  }
}
