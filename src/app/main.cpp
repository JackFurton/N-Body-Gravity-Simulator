#include <charconv>
#include <cstdio>
#include <exception>
#include <string_view>

#include "app/app.hpp"

namespace {

void usage() {
  std::puts(
      "usage: nbody [options]\n"
      "  --scenario <disk|collision|plummer|collapse>   initial conditions (default disk)\n"
      "  --bodies <n>                                   body count (default 4096)\n"
      "  --seed <n>                                     RNG seed (default 42)\n"
      "  --size <w>x<h>                                 window / image size (default 1600x1000)\n"
      "  --capture <file.png>                           render one frame to PNG and exit\n"
      "  --capture-step <n>                             ...after the sim reaches step n (default 0)\n"
      "  --headless                                     render offscreen, no window (needs --capture)");
}

template <class T>
bool parse_number(std::string_view s, T& out) {
  const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
  return ec == std::errc{} && ptr == s.data() + s.size();
}

bool parse_size(std::string_view s, int& w, int& h) {
  const auto x = s.find('x');
  return x != std::string_view::npos && parse_number(s.substr(0, x), w) && parse_number(s.substr(x + 1), h) &&
         w > 0 && h > 0;
}

}  // namespace

int main(int argc, char** argv) {
  nbody::AppOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const char* raw = i + 1 < argc ? argv[i + 1] : nullptr;
    const std::string_view value = raw ? raw : "";
    bool ok = raw != nullptr;
    bool takes_value = true;

    if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    } else if (arg == "--headless") {
      options.headless = true;
      ok = true;
      takes_value = false;
    } else if (arg == "--scenario") {
      const auto kind = nbody::scenario_from_id(value);
      ok = ok && kind.has_value();
      if (ok) options.scenario.kind = *kind;
    } else if (arg == "--bodies") {
      ok = ok && parse_number(value, options.scenario.body_count) && options.scenario.body_count >= 2;
    } else if (arg == "--seed") {
      ok = ok && parse_number(value, options.scenario.seed);
    } else if (arg == "--size") {
      ok = ok && parse_size(value, options.width, options.height);
    } else if (arg == "--capture") {
      if (ok) options.capture_path = std::string(value);
    } else if (arg == "--capture-step") {
      ok = ok && parse_number(value, options.capture_step);
    } else {
      ok = false;
    }
    if (!ok) {
      std::fprintf(stderr, "bad argument: %s\n", argv[i]);
      usage();
      return 2;
    }
    if (takes_value) ++i;
  }
  if (options.headless && !options.capture_path) {
    std::fputs("--headless needs --capture <file.png>\n", stderr);
    return 2;
  }

  try {
    if (options.headless) return nbody::run_headless(options);
    nbody::App app(options);
    return app.run();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
}
