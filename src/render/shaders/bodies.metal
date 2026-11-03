#include <metal_stdlib>
using namespace metal;

// Must match BodyUniforms in renderer_metal.mm.
struct BodyUniforms {
  float4x4 view;
  float4x4 proj;
  float2 viewport;        // drawable size in pixels
  float star_size;        // billboard radius in world units
  float min_pixel_size;   // billboards never shrink below this radius on screen
  float brightness;
  float speed_scale;      // 1 / (speed that maps to the hot end of the palette)
  float min_flux;
  float pad;
};

struct BodyVertexOut {
  float4 position [[position]];
  float2 uv;
  float3 color;
  float intensity;
};

static float3 speed_palette(float t) {
  t = saturate(t);
  const float3 slow = float3(1.00, 0.45, 0.18);
  const float3 mid  = float3(1.00, 0.92, 0.75);
  const float3 fast = float3(0.45, 0.65, 1.00);
  return t < 0.5 ? mix(slow, mid, t * 2.0) : mix(mid, fast, t * 2.0 - 1.0);
}

vertex BodyVertexOut body_vertex(uint vid [[vertex_id]],
                                 uint iid [[instance_id]],
                                 const device float4* bodies [[buffer(0)]],
                                 constant BodyUniforms& u [[buffer(1)]]) {
  const float2 corner = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? 1.0 : -1.0);
  const float4 body = bodies[iid];

  float4 view_pos = u.view * float4(body.xyz, 1.0);
  const float depth = max(-view_pos.z, 1e-4);
  const float min_world = u.min_pixel_size * 2.0 * depth / (u.viewport.y * u.proj[1][1]);
  const float size = max(u.star_size, min_world);
  view_pos.xy += corner * size;

  BodyVertexOut out;
  out.position = u.proj * view_pos;
  out.uv = corner;
  out.color = speed_palette(body.w * u.speed_scale);
  // Spread the same light over a larger sprite when it is clamped to the pixel minimum, so
  // distant clusters dim with distance instead of blooming into solid blobs.
  const float ratio = u.star_size / size;
  out.intensity = u.brightness * max(ratio * ratio, u.min_flux);
  return out;
}

fragment float4 body_fragment(BodyVertexOut in [[stage_in]]) {
  const float r2 = dot(in.uv, in.uv);
  if (r2 > 1.0) discard_fragment();
  const float a = exp(-4.0 * r2) * in.intensity;
  return float4(in.color * a, a);
}
