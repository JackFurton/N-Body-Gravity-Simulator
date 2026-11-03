#include "sim/force_kernels.hpp"

#include <algorithm>
#include <cmath>

#include "sim/simd.hpp"

namespace nbody {
namespace {

void scalar_range(const ForceInputs& in, const ForceOutputs& out, std::size_t i_begin, std::size_t i_end) {
  const float eps2 = in.softening * in.softening;
  const float* __restrict px = in.px;
  const float* __restrict py = in.py;
  const float* __restrict pz = in.pz;
  const float* __restrict m = in.mass;
  for (std::size_t i = i_begin; i < i_end; ++i) {
    const float xi = px[i], yi = py[i], zi = pz[i];
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    // j == i is included on purpose: dx = 0 contributes nothing once eps2 > 0, and a branch-free
    // loop is what lets every variant vectorise.
    for (std::size_t j = 0; j < in.n; ++j) {
      const float dx = px[j] - xi;
      const float dy = py[j] - yi;
      const float dz = pz[j] - zi;
      const float r2 = dx * dx + dy * dy + dz * dz + eps2;
      const float inv_r = 1.0f / std::sqrt(r2);
      const float s = m[j] * inv_r * inv_r * inv_r;
      ax += dx * s;
      ay += dy * s;
      az += dz * s;
    }
    out.ax[i] = in.G * ax;
    out.ay[i] = in.G * ay;
    out.az[i] = in.G * az;
  }
}

// Targets live in lanes and sources are broadcast one at a time, so there is no horizontal
// reduction and no padding of the source arrays. Every op is lane-wise, so a body's result does
// not depend on which block or lane it lands in.
template <int B, bool Exact>
void simd_block(const ForceInputs& in, const float* x, const float* y, const float* z, float* ax_out, float* ay_out,
                float* az_out) {
  using namespace simd;
  const f32x4 eps2 = broadcast(in.softening * in.softening);
  f32x4 xi[B], yi[B], zi[B], ax[B], ay[B], az[B];
  for (int b = 0; b < B; ++b) {
    xi[b] = load(x + 4 * b);
    yi[b] = load(y + 4 * b);
    zi[b] = load(z + 4 * b);
    ax[b] = ay[b] = az[b] = broadcast(0.0f);
  }
  for (std::size_t j = 0; j < in.n; ++j) {
    const f32x4 xj = load_broadcast(in.px + j);
    const f32x4 yj = load_broadcast(in.py + j);
    const f32x4 zj = load_broadcast(in.pz + j);
    const f32x4 mj = load_broadcast(in.mass + j);
    for (int b = 0; b < B; ++b) {
      const f32x4 dx = xj - xi[b];
      const f32x4 dy = yj - yi[b];
      const f32x4 dz = zj - zi[b];
      const f32x4 r2 = fma(dx, dx, fma(dy, dy, fma(dz, dz, eps2)));
      f32x4 s;
      if constexpr (Exact) {
        s = mj / (r2 * simd::sqrt(r2));
      } else {
        const f32x4 inv_r = rsqrt(r2);
        s = mj * (inv_r * inv_r * inv_r);
      }
      ax[b] = fma(dx, s, ax[b]);
      ay[b] = fma(dy, s, ay[b]);
      az[b] = fma(dz, s, az[b]);
    }
  }
  const f32x4 G = broadcast(in.G);
  for (int b = 0; b < B; ++b) {
    store(ax_out + 4 * b, G * ax[b]);
    store(ay_out + 4 * b, G * ay[b]);
    store(az_out + 4 * b, G * az[b]);
  }
}

template <int B, bool Exact>
void simd_range(const ForceInputs& in, const ForceOutputs& out, std::size_t i_begin, std::size_t i_end) {
  std::size_t i = i_begin;
  for (; i + 4 * B <= i_end; i += 4 * B)
    simd_block<B, Exact>(in, in.px + i, in.py + i, in.pz + i, out.ax + i, out.ay + i, out.az + i);
  for (; i + 4 <= i_end; i += 4)
    simd_block<1, Exact>(in, in.px + i, in.py + i, in.pz + i, out.ax + i, out.ay + i, out.az + i);
  if (i == i_end) return;

  // Last 1-3 targets go through the same lane math via a zero-padded copy, never the scalar loop,
  // so results are bitwise identical however the range is split.
  const std::size_t r = i_end - i;
  alignas(16) float x[4] = {}, y[4] = {}, z[4] = {}, ax[4], ay[4], az[4];
  std::copy_n(in.px + i, r, x);
  std::copy_n(in.py + i, r, y);
  std::copy_n(in.pz + i, r, z);
  simd_block<1, Exact>(in, x, y, z, ax, ay, az);
  std::copy_n(ax, r, out.ax + i);
  std::copy_n(ay, r, out.ay + i);
  std::copy_n(az, r, out.az + i);
}

}  // namespace

std::vector<glm::dvec3> reference_direct_forces(const ForceInputs& in) {
  std::vector<glm::dvec3> acc(in.n, glm::dvec3(0.0));
  const double eps2 = double(in.softening) * in.softening;
  for (std::size_t i = 0; i < in.n; ++i) {
    for (std::size_t j = 0; j < in.n; ++j) {
      const glm::dvec3 d{double(in.px[j]) - in.px[i], double(in.py[j]) - in.py[i], double(in.pz[j]) - in.pz[i]};
      const double r2 = d.x * d.x + d.y * d.y + d.z * d.z + eps2;
      acc[i] += d * (double(in.mass[j]) / (r2 * std::sqrt(r2)));
    }
    acc[i] *= double(in.G);
  }
  return acc;
}

void compute_direct_forces(ForceKernel kernel, const ForceInputs& in, const ForceOutputs& out, std::size_t i_begin,
                           std::size_t i_end) {
  switch (kernel) {
    case ForceKernel::Scalar: return scalar_range(in, out, i_begin, i_end);
    case ForceKernel::SimdRsqrtX4: return simd_range<4, false>(in, out, i_begin, i_end);
    case ForceKernel::SimdX1: return simd_range<1, true>(in, out, i_begin, i_end);
    case ForceKernel::SimdX2: return simd_range<2, true>(in, out, i_begin, i_end);
    case ForceKernel::SimdX4: return simd_range<4, true>(in, out, i_begin, i_end);
  }
}

}  // namespace nbody
