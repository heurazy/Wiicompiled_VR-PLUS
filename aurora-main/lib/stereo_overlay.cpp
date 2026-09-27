#include "stereo_overlay.hpp"

#include "gfx/common.hpp"
#include "gfx/stereo_replay.hpp"
#include "imgui.hpp"
#include "webgpu/gpu.hpp"

#include <aurora/aurora.h>
#include <imgui.h>

#include "tracy/Tracy.hpp"

#include <array>
#include <atomic>
#include <cmath>

namespace aurora::stereo_overlay {
namespace {

using webgpu::g_device;
using webgpu::g_queue;

// Uploaded as-is into a WGSL mat4x4<f32>, like the GX uniforms' matrices.
static_assert(sizeof(Mat4x4<float>) == 64);

// The panel's corners, (-1, 1) top left to (1, -1) bottom right, carried through
// one clip-from-panel matrix per eye. UVs are interpolated perspective-correct,
// so the texture stays straight on a panel seen at an angle. The texture holds
// premultiplied colour, which is what ImGui's blending leaves in a cleared target.
constexpr const char* kShader = R"""(
struct Panel {
    clip_from_panel: mat4x4<f32>,
};
@group(0) @binding(0)
var<uniform> panel: Panel;
@group(0) @binding(1)
var panel_sampler: sampler;
@group(0) @binding(2)
var panel_texture: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

var<private> corners: array<vec2<f32>, 4> = array<vec2<f32>, 4>(
    vec2(-1.0, 1.0),
    vec2(-1.0, -1.0),
    vec2(1.0, 1.0),
    vec2(1.0, -1.0),
);

@vertex
fn vs_main(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    let corner = corners[vtxIdx];
    var out: VertexOutput;
    // Row-vector convention, like the GX shaders: m0..m3 are the clip x/y/z/w rows.
    out.pos = vec4<f32>(corner, 0.0, 1.0) * panel.clip_from_panel;
    out.uv = vec2<f32>(0.5 + 0.5 * corner.x, 0.5 - 0.5 * corner.y);
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    return textureSample(panel_texture, panel_sampler, in.uv);
}
)""";

struct State {
  webgpu::TextureWithSampler panel;
  wgpu::BindGroup uiBinding;
  wgpu::RenderPipeline pipeline;
  wgpu::BindGroupLayout bindGroupLayout;
  wgpu::TextureFormat pipelineFormat = wgpu::TextureFormat::Undefined;
  std::array<wgpu::Buffer, AURORA_STEREO_EYE_COUNT> uniforms;
  std::array<wgpu::BindGroup, AURORA_STEREO_EYE_COUNT> bindGroups;
  float widthFraction = 0.f;
  bool visible = false;
  // Stands in for the panel in a layer while it is not showing.
  webgpu::TextureWithSampler transparent;
  bool transparentCleared = false;
};
State g_state;
std::atomic_bool g_layerMode{false};

bool ensure_pipeline() {
  auto& state = g_state;
  const auto format = webgpu::g_graphicsConfig.surfaceConfiguration.format;
  if (state.pipeline && state.pipelineFormat == format) {
    return true;
  }
  state.pipeline = {};
  state.bindGroups = {};

  wgpu::ShaderSourceWGSL source{};
  source.code = kShader;
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &source,
      .label = "Headset panel module",
  };
  const auto module = g_device.CreateShaderModule(&moduleDescriptor);

  const std::array layoutEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Vertex,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::Uniform,
                  .minBindingSize = sizeof(Mat4x4<float>),
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler =
              wgpu::SamplerBindingLayout{
                  .type = wgpu::SamplerBindingType::Filtering,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor layoutDescriptor{
      .label = "Headset panel bind group layout",
      .entryCount = layoutEntries.size(),
      .entries = layoutEntries.data(),
  };
  state.bindGroupLayout = g_device.CreateBindGroupLayout(&layoutDescriptor);
  const wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor{
      .label = "Headset panel pipeline layout",
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &state.bindGroupLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&pipelineLayoutDescriptor);

  constexpr wgpu::BlendComponent kPremultipliedOver{
      .operation = wgpu::BlendOperation::Add,
      .srcFactor = wgpu::BlendFactor::One,
      .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
  };
  const wgpu::BlendState blend{
      .color = kPremultipliedOver,
      .alpha = kPremultipliedOver,
  };
  const std::array colorTargets{wgpu::ColorTargetState{
      .format = format,
      .blend = &blend,
      .writeMask = wgpu::ColorWriteMask::All,
  }};
  const wgpu::FragmentState fragmentState{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };
  const wgpu::RenderPipelineDescriptor pipelineDescriptor{
      .label = "Headset panel pipeline",
      .layout = pipelineLayout,
      .vertex =
          wgpu::VertexState{
              .module = module,
              .entryPoint = "vs_main",
          },
      .primitive =
          wgpu::PrimitiveState{
              .topology = wgpu::PrimitiveTopology::TriangleStrip,
              .cullMode = wgpu::CullMode::None,
          },
      .multisample =
          wgpu::MultisampleState{
              .count = 1,
              .mask = UINT32_MAX,
          },
      .fragment = &fragmentState,
  };
  state.pipeline = g_device.CreateRenderPipeline(&pipelineDescriptor);
  if (!state.pipeline) {
    return false;
  }
  for (auto& uniform : state.uniforms) {
    if (!uniform) {
      const wgpu::BufferDescriptor bufferDescriptor{
          .label = "Headset panel uniform",
          .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
          .size = sizeof(Mat4x4<float>),
      };
      uniform = g_device.CreateBuffer(&bufferDescriptor);
    }
  }
  state.pipelineFormat = format;
  return true;
}

void composite(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& target, const Mat4x4<float>& clipFromPanel,
               uint32_t eyeIndex) noexcept {
  auto& state = g_state;
  if (!state.visible || !state.pipeline || eyeIndex >= AURORA_STEREO_EYE_COUNT || !target) {
    return;
  }
  auto& bindGroup = state.bindGroups[eyeIndex];
  if (!bindGroup) {
    const std::array entries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .buffer = state.uniforms[eyeIndex],
            .size = sizeof(Mat4x4<float>),
        },
        wgpu::BindGroupEntry{
            .binding = 1,
            .sampler = state.panel.sampler,
        },
        wgpu::BindGroupEntry{
            .binding = 2,
            .textureView = state.panel.view,
        },
    };
    const wgpu::BindGroupDescriptor descriptor{
        .label = "Headset panel bind group",
        .layout = state.bindGroupLayout,
        .entryCount = entries.size(),
        .entries = entries.data(),
    };
    bindGroup = g_device.CreateBindGroup(&descriptor);
  }
  // Each eye has its own uniform, and every pass that reads it is submitted
  // before this worker writes it again.
  g_queue.WriteBuffer(state.uniforms[eyeIndex], 0, &clipFromPanel, sizeof(clipFromPanel));

  const std::array attachments{
      wgpu::RenderPassColorAttachment{
          .view = target,
          .loadOp = wgpu::LoadOp::Load,
          .storeOp = wgpu::StoreOp::Store,
      },
  };
  const wgpu::RenderPassDescriptor descriptor{
      .label = eyeIndex == 0 ? "Headset panel left eye" : "Headset panel right eye",
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
      .timestampWrites = gfx::gpu_timing_pass(gfx::GpuTimingCategory::Panel),
  };
  const auto pass = encoder.BeginRenderPass(&descriptor);
  pass.SetPipeline(state.pipeline);
  pass.SetBindGroup(0, bindGroup, 0, nullptr);
  pass.Draw(4);
  pass.End();
}

float panel_aspect() noexcept {
  const auto& size = g_state.panel.size;
  return size.height != 0 ? static_cast<float>(size.width) / static_cast<float>(size.height) : 0.f;
}

} // namespace

wgpu::CommandBuffer prepare(ImDrawData* drawData, float widthFraction) noexcept {
  ZoneScoped;
  auto& state = g_state;
  state.visible = false;
  if (drawData == nullptr || !(widthFraction > 0.f)) {
    return {};
  }
  const auto width = static_cast<uint32_t>(std::lround(drawData->DisplaySize.x * drawData->FramebufferScale.x));
  const auto height = static_cast<uint32_t>(std::lround(drawData->DisplaySize.y * drawData->FramebufferScale.y));
  if (width == 0 || height == 0 || !ensure_pipeline()) {
    return {};
  }
  const auto format = webgpu::g_graphicsConfig.surfaceConfiguration.format;
  if (!state.panel.texture || state.panel.size.width != width || state.panel.size.height != height ||
      state.panel.format != format) {
    state.panel = webgpu::create_render_texture(width, height, false);
    state.uiBinding = {};
    state.bindGroups = {};
  }
  // The ImGui backend sets its viewport from the draw data, so a texture the
  // device clamped to a smaller size cannot hold the pass.
  if (state.panel.size.width != width || state.panel.size.height != height) {
    return {};
  }

  const wgpu::CommandEncoderDescriptor encoderDescriptor{
      .label = "Headset panel encoder",
  };
  auto encoder = g_device.CreateCommandEncoder(&encoderDescriptor);
  const std::array attachments{
      wgpu::RenderPassColorAttachment{
          .view = state.panel.view,
          .loadOp = wgpu::LoadOp::Clear,
          .storeOp = wgpu::StoreOp::Store,
          .clearValue = {.r = 0.0, .g = 0.0, .b = 0.0, .a = 0.0},
      },
  };
  const wgpu::RenderPassDescriptor passDescriptor{
      .label = "Headset panel ImGui pass",
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
      .timestampWrites = gfx::gpu_timing_pass(gfx::GpuTimingCategory::Panel),
  };
  bool drawn = false;
  {
    const auto pass = encoder.BeginRenderPass(&passDescriptor);
    drawn = imgui::render_draw_data(pass, drawData);
    pass.End();
  }
  if (!drawn) {
    return {};
  }
  state.widthFraction = widthFraction;
  state.visible = true;
  return encoder.Finish();
}

void composite_immersive(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& eye,
                         const Mat4x4<float>& eyeFrustum, const Mat3x4<float>& viewFromCenter,
                         uint32_t eyeIndex) noexcept {
  if (!g_state.visible || layer_mode()) {
    return;
  }
  float screenWidth = 0.f;
  float screenDistance = 0.f;
  gfx::get_stereo_hud_screen_size(screenWidth, screenDistance);
  const auto panel = gfx::stereo_replay::overlay_panel_on_screen(screenWidth, screenDistance, g_state.widthFraction,
                                                                 panel_aspect());
  if (!panel.valid()) {
    return;
  }
  composite(encoder, eye, gfx::stereo_replay::compose_overlay_panel_projection(eyeFrustum, viewFromCenter, panel),
            eyeIndex);
}

void composite_flat(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& eye, const wgpu::Extent3D& size,
                    uint32_t eyeIndex) noexcept {
  if (!g_state.visible || layer_mode() || size.width == 0 || size.height == 0) {
    return;
  }
  const float imageAspect = static_cast<float>(size.width) / static_cast<float>(size.height);
  composite(encoder, eye,
            gfx::stereo_replay::overlay_panel_flat_projection(g_state.widthFraction, panel_aspect(), imageAspect),
            eyeIndex);
}

void set_layer_mode(bool enabled) noexcept { g_layerMode.store(enabled, std::memory_order_release); }

bool layer_mode() noexcept { return g_layerMode.load(std::memory_order_acquire); }

bool layer_source(const wgpu::CommandEncoder& encoder, uint32_t width, uint32_t height, stereo::EyeImage& out) noexcept {
  auto& state = g_state;
  const auto format = webgpu::g_graphicsConfig.surfaceConfiguration.format;
  if (width == 0 || height == 0) {
    return false;
  }
  if (state.visible && state.panel.texture && state.panel.size.width == width && state.panel.size.height == height &&
      state.panel.format == format) {
    out = {.texture = &state.panel.texture, .view = &state.panel.view, .size = state.panel.size, .format = format};
    return true;
  }
  if (!state.transparent.texture || state.transparent.size.width != width ||
      state.transparent.size.height != height || state.transparent.format != format) {
    state.transparent = webgpu::create_render_texture(width, height, false);
    state.transparentCleared = false;
    if (state.transparent.size.width != width || state.transparent.size.height != height) {
      state.transparent = {};
      return false;
    }
  }
  if (!state.transparentCleared) {
    const std::array attachments{
        wgpu::RenderPassColorAttachment{
            .view = state.transparent.view,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = {.r = 0.0, .g = 0.0, .b = 0.0, .a = 0.0},
        },
    };
    const wgpu::RenderPassDescriptor descriptor{
        .label = "Headset panel layer clear",
        .colorAttachmentCount = attachments.size(),
        .colorAttachments = attachments.data(),
    };
    encoder.BeginRenderPass(&descriptor).End();
    state.transparentCleared = true;
  }
  out = {.texture = &state.transparent.texture, .view = &state.transparent.view, .size = state.transparent.size,
         .format = format};
  return true;
}

bool prepared_source(webgpu::PresentSource& source) noexcept {
  if (!g_state.visible || !g_state.panel.texture) return false;
  if (!g_state.uiBinding) g_state.uiBinding=webgpu::create_copy_bind_group(g_state.panel.view,g_state.panel.sampler);
  source = {.bindGroup=g_state.uiBinding,
            .texture=g_state.panel.texture, .size=g_state.panel.size, .format=g_state.panel.format};
  return true;
}

void shutdown() noexcept { g_state = {}; }

} // namespace aurora::stereo_overlay
