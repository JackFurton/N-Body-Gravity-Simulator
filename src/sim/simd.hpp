#pragma once

#include <cmath>

#if defined(__ARM_NEON) && !defined(NBODY_SIMD_PORTABLE)
#include <arm_neon.h>
#endif

// Four float lanes. NEON on arm64, plain arrays elsewhere or with NBODY_SIMD_PORTABLE (the compiler usually vectorises
// those too). Kernels are written once against this and never touch intrinsics directly.
namespace nbody::simd {

#if defined(__ARM_NEON) && !defined(NBODY_SIMD_PORTABLE)

struct f32x4 {
  float32x4_t v;
};

inline f32x4 load(const float* p) { return {vld1q_f32(p)}; }
inline f32x4 load_broadcast(const float* p) { return {vld1q_dup_f32(p)}; }
inline f32x4 broadcast(float x) { return {vdupq_n_f32(x)}; }
inline void store(float* p, f32x4 a) { vst1q_f32(p, a.v); }

inline f32x4 operator+(f32x4 a, f32x4 b) { return {vaddq_f32(a.v, b.v)}; }
inline f32x4 operator-(f32x4 a, f32x4 b) { return {vsubq_f32(a.v, b.v)}; }
inline f32x4 operator*(f32x4 a, f32x4 b) { return {vmulq_f32(a.v, b.v)}; }
inline f32x4 operator/(f32x4 a, f32x4 b) { return {vdivq_f32(a.v, b.v)}; }
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c) { return {vfmaq_f32(c.v, a.v, b.v)}; }  // a * b + c
inline f32x4 sqrt(f32x4 a) { return {vsqrtq_f32(a.v)}; }

// Hardware estimate (~8 bits) plus two Newton-Raphson steps, close to full float precision.
inline f32x4 rsqrt(f32x4 x) {
  float32x4_t e = vrsqrteq_f32(x.v);
  e = vmulq_f32(e, vrsqrtsq_f32(vmulq_f32(x.v, e), e));
  e = vmulq_f32(e, vrsqrtsq_f32(vmulq_f32(x.v, e), e));
  return {e};
}

#else

struct f32x4 {
  float v[4];
};

#define NBODY_SIMD_MAP(expr)                  \
  f32x4 r;                                    \
  for (int k = 0; k < 4; ++k) r.v[k] = (expr); \
  return r

inline f32x4 load(const float* p) { NBODY_SIMD_MAP(p[k]); }
inline f32x4 load_broadcast(const float* p) { NBODY_SIMD_MAP(*p); }
inline f32x4 broadcast(float x) { NBODY_SIMD_MAP(x); }
inline void store(float* p, f32x4 a) {
  for (int k = 0; k < 4; ++k) p[k] = a.v[k];
}

inline f32x4 operator+(f32x4 a, f32x4 b) { NBODY_SIMD_MAP(a.v[k] + b.v[k]); }
inline f32x4 operator-(f32x4 a, f32x4 b) { NBODY_SIMD_MAP(a.v[k] - b.v[k]); }
inline f32x4 operator*(f32x4 a, f32x4 b) { NBODY_SIMD_MAP(a.v[k] * b.v[k]); }
inline f32x4 operator/(f32x4 a, f32x4 b) { NBODY_SIMD_MAP(a.v[k] / b.v[k]); }
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c) { NBODY_SIMD_MAP(std::fma(a.v[k], b.v[k], c.v[k])); }
inline f32x4 sqrt(f32x4 a) { NBODY_SIMD_MAP(std::sqrt(a.v[k])); }
inline f32x4 rsqrt(f32x4 a) { NBODY_SIMD_MAP(1.0f / std::sqrt(a.v[k])); }

#undef NBODY_SIMD_MAP

#endif

}  // namespace nbody::simd
