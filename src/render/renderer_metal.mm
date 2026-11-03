#include "render/renderer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <simd/simd.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_metal.h>

#include "embedded_shaders.hpp"

namespace nbody {
namespace {

constexpr int kFramesInFlight = 3;
constexpr MTLPixelFormat kColorFormat = MTLPixelFormatBGRA8Unorm;

// Must match BodyUniforms in shaders/bodies.metal.
struct BodyUniforms {
  simd_float4x4 view;
  simd_float4x4 proj;
  simd_float2 viewport;
  float star_size;
  float min_pixel_size;
  float brightness;
  float speed_scale;
  float min_flux;
  float pad;
};
static_assert(sizeof(BodyUniforms) == 160);

simd_float4x4 to_simd(const glm::mat4& m) {
  simd_float4x4 out;
  static_assert(sizeof(out) == sizeof(m));
  std::memcpy(&out, &m, sizeof(out));  // both column-major
  return out;
}

bool write_png(const char* path, const void* bgra, int width, int height, size_t bytes_per_row) {
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef ctx = CGBitmapContextCreate(const_cast<void*>(bgra), size_t(width), size_t(height), 8, bytes_per_row,
                                           space, CGBitmapInfo(kCGImageAlphaNoneSkipFirst) | kCGBitmapByteOrder32Little);
  CGImageRef image = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;
  bool ok = false;
  if (image) {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    CGImageDestinationRef dest =
        CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
    if (dest) {
      CGImageDestinationAddImage(dest, image, nullptr);
      ok = CGImageDestinationFinalize(dest);
      CFRelease(dest);
    }
    CGImageRelease(image);
  }
  if (ctx) CGContextRelease(ctx);
  CGColorSpaceRelease(space);
  return ok;
}

}  // namespace

struct Renderer::Impl {
  GLFWwindow* window = nullptr;  // null when headless
  bool capture_enabled = false;
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  CAMetalLayer* layer;
  id<MTLTexture> offscreen;
  id<MTLRenderPipelineState> body_pipeline;
  MTLRenderPassDescriptor* pass;
  dispatch_semaphore_t frames_in_flight;
  std::array<id<MTLBuffer>, kFramesInFlight> body_buffers{};
  int frame_index = 0;
  id<CAMetalDrawable> drawable;

  void init_device_and_pipeline();
  id<MTLTexture> target() const { return drawable ? drawable.texture : offscreen; }
};

void Renderer::Impl::init_device_and_pipeline() {
  device = MTLCreateSystemDefaultDevice();
  if (!device) throw std::runtime_error("no Metal device available");
  queue = [device newCommandQueue];

  NSError* error = nil;
  id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:kBodiesShaderSource]
                                                options:[MTLCompileOptions new]
                                                  error:&error];
  if (!library) throw std::runtime_error(std::string("shader compile failed: ") + error.localizedDescription.UTF8String);

  MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
  desc.label = @"bodies";
  desc.vertexFunction = [library newFunctionWithName:@"body_vertex"];
  desc.fragmentFunction = [library newFunctionWithName:@"body_fragment"];
  MTLRenderPipelineColorAttachmentDescriptor* color = desc.colorAttachments[0];
  color.pixelFormat = kColorFormat;
  // Pure additive: overlapping stars sum, draw order never matters, no depth buffer needed.
  color.blendingEnabled = YES;
  color.rgbBlendOperation = color.alphaBlendOperation = MTLBlendOperationAdd;
  color.sourceRGBBlendFactor = color.sourceAlphaBlendFactor = MTLBlendFactorOne;
  color.destinationRGBBlendFactor = color.destinationAlphaBlendFactor = MTLBlendFactorOne;
  body_pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
  if (!body_pipeline)
    throw std::runtime_error(std::string("pipeline creation failed: ") + error.localizedDescription.UTF8String);

  pass = [MTLRenderPassDescriptor new];
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0.004, 0.005, 0.010, 1.0);
  frames_in_flight = dispatch_semaphore_create(kFramesInFlight);
}

Renderer::Renderer(GLFWwindow* window, bool enable_capture) : impl_(std::make_unique<Impl>()) {
  Impl& r = *impl_;
  r.window = window;
  r.capture_enabled = enable_capture;
  r.init_device_and_pipeline();

  r.layer = [CAMetalLayer layer];
  r.layer.device = r.device;
  r.layer.pixelFormat = kColorFormat;
  r.layer.framebufferOnly = !enable_capture;  // capture blits out of the drawable
  NSWindow* ns_window = glfwGetCocoaWindow(window);
  ns_window.contentView.layer = r.layer;
  ns_window.contentView.wantsLayer = YES;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui::GetStyle().WindowRounding = 6.0f;
  ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.85f;
  ImGui_ImplGlfw_InitForOther(window, true);  // chains any GLFW callbacks installed before this
  ImGui_ImplMetal_Init(r.device);
}

Renderer::Renderer(glm::ivec2 size) : impl_(std::make_unique<Impl>()) {
  Impl& r = *impl_;
  r.capture_enabled = true;
  r.init_device_and_pipeline();
  MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:kColorFormat
                                                                                   width:NSUInteger(size.x)
                                                                                  height:NSUInteger(size.y)
                                                                               mipmapped:NO];
  desc.usage = MTLTextureUsageRenderTarget;
  desc.storageMode = MTLStorageModePrivate;
  r.offscreen = [r.device newTextureWithDescriptor:desc];
}

Renderer::~Renderer() {
  // Let in-flight command buffers finish before their buffers and the ImGui backend go away.
  for (int i = 0; i < kFramesInFlight; ++i) dispatch_semaphore_wait(impl_->frames_in_flight, DISPATCH_TIME_FOREVER);
  for (int i = 0; i < kFramesInFlight; ++i) dispatch_semaphore_signal(impl_->frames_in_flight);
  if (!impl_->window) return;
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

glm::ivec2 Renderer::framebuffer_size() const {
  if (!impl_->window) return {int(impl_->offscreen.width), int(impl_->offscreen.height)};
  int w = 0, h = 0;
  glfwGetFramebufferSize(impl_->window, &w, &h);
  return {w, h};
}

std::string Renderer::device_name() const { return impl_->device.name.UTF8String; }

bool Renderer::begin_frame() {
  Impl& r = *impl_;
  if (!r.window) {
    r.pass.colorAttachments[0].texture = r.offscreen;
    return true;
  }
  const glm::ivec2 size = framebuffer_size();
  if (size.x <= 0 || size.y <= 0) return false;
  r.layer.drawableSize = CGSizeMake(size.x, size.y);

  r.drawable = [r.layer nextDrawable];
  if (!r.drawable) return false;
  r.pass.colorAttachments[0].texture = r.drawable.texture;

  ImGui_ImplMetal_NewFrame(r.pass);
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  return true;
}

bool Renderer::end_frame(const FrameData& frame, const char* capture_path) {
  Impl& r = *impl_;
  const bool ui = r.window != nullptr;
  if (ui) ImGui::Render();

  dispatch_semaphore_wait(r.frames_in_flight, DISPATCH_TIME_FOREVER);
  r.frame_index = (r.frame_index + 1) % kFramesInFlight;

  const NSUInteger n = frame.bodies.size();
  const NSUInteger bytes = std::max<NSUInteger>(n, 1) * sizeof(glm::vec4);
  __strong id<MTLBuffer>& buffer = r.body_buffers[size_t(r.frame_index)];
  if (!buffer || buffer.length < bytes)
    buffer = [r.device newBufferWithLength:bytes + bytes / 2 options:MTLResourceStorageModeShared];
  if (n) std::memcpy(buffer.contents, frame.bodies.data(), n * sizeof(glm::vec4));

  const RenderSettings& s = frame.settings;
  id<MTLTexture> target = r.target();
  const BodyUniforms uniforms{
      .view = to_simd(frame.view),
      .proj = to_simd(frame.proj),
      .viewport = simd_make_float2(float(target.width), float(target.height)),
      .star_size = s.star_size,
      .min_pixel_size = s.min_pixel_size,
      .brightness = s.brightness,
      .speed_scale = 1.0f / std::max(frame.rms_speed * s.color_speed, 1e-6f),
      .min_flux = s.min_flux,
      .pad = 0.0f,
  };

  id<MTLCommandBuffer> cmd = [r.queue commandBuffer];
  id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:r.pass];
  if (n) {
    [enc setRenderPipelineState:r.body_pipeline];
    [enc setVertexBuffer:buffer offset:0 atIndex:0];
    [enc setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:n];
  }
  if (ui) ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmd, enc);
  [enc endEncoding];

  id<MTLBuffer> readback = nil;
  NSUInteger readback_row = 0;
  if (capture_path && r.capture_enabled) {
    readback_row = target.width * 4;
    readback = [r.device newBufferWithLength:readback_row * target.height options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:target
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(target.width, target.height, 1)
                        toBuffer:readback
               destinationOffset:0
          destinationBytesPerRow:readback_row
        destinationBytesPerImage:readback_row * target.height];
    [blit endEncoding];
  }

  dispatch_semaphore_t sem = r.frames_in_flight;
  [cmd addCompletedHandler:^(id<MTLCommandBuffer>) { dispatch_semaphore_signal(sem); }];
  if (r.drawable) [cmd presentDrawable:r.drawable];
  [cmd commit];
  r.drawable = nil;

  if (!readback) return capture_path == nullptr;
  [cmd waitUntilCompleted];
  return write_png(capture_path, readback.contents, int(target.width), int(target.height), readback_row);
}

}  // namespace nbody
