#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace nbody {

// Single-producer / single-consumer handoff of the latest value. Neither side ever blocks: the
// writer always has a private back buffer, the reader keeps its front buffer until it asks for a
// newer one, and the middle slot is swapped atomically between them.
template <class T>
class TripleBuffer {
 public:
  // Writer side.
  T& back() { return slots_[back_]; }
  void publish() {
    const auto prev = middle_.exchange(std::uint8_t(back_ | kFresh), std::memory_order_acq_rel);
    back_ = prev & kIndexMask;
  }

  // Reader side. Returns true if front() now refers to a newer value.
  bool acquire() {
    if (!(middle_.load(std::memory_order_relaxed) & kFresh)) return false;
    const auto prev = middle_.exchange(front_, std::memory_order_acq_rel);
    front_ = prev & kIndexMask;
    return true;
  }
  const T& front() const { return slots_[front_]; }

 private:
  static constexpr std::uint8_t kFresh = 0x4;
  static constexpr std::uint8_t kIndexMask = 0x3;

  std::array<T, 3> slots_{};
  std::uint8_t back_ = 0;
  std::atomic<std::uint8_t> middle_{1};
  std::uint8_t front_ = 2;
};

}  // namespace nbody
