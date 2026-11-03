#include <doctest/doctest.h>

#include <thread>

#include "sim/triple_buffer.hpp"

using nbody::TripleBuffer;

TEST_CASE("reader sees only the most recent publish") {
  TripleBuffer<int> tb;
  CHECK_FALSE(tb.acquire());
  tb.back() = 1;
  tb.publish();
  tb.back() = 2;
  tb.publish();
  CHECK(tb.acquire());
  CHECK(tb.front() == 2);
  CHECK_FALSE(tb.acquire());
  CHECK(tb.front() == 2);
}

TEST_CASE("concurrent writer never hands the reader a torn or stale value") {
  struct Pair {
    long a = 0, b = 0;
  };
  TripleBuffer<Pair> tb;
  constexpr long kWrites = 200000;
  std::jthread writer([&] {
    for (long i = 1; i <= kWrites; ++i) {
      tb.back() = {i, -i};
      tb.publish();
    }
  });
  long last = 0;
  bool consistent = true, monotonic = true;
  while (last < kWrites) {
    if (!tb.acquire()) continue;
    const Pair p = tb.front();
    consistent &= p.a == -p.b;
    monotonic &= p.a > last;
    last = p.a;
  }
  CHECK(consistent);
  CHECK(monotonic);
}
