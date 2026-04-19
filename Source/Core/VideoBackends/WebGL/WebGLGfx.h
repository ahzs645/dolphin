// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>

#include <GLES3/gl3.h>

#include "VideoBackends/WebGL/WebGLContext.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/Constants.h"

namespace WebGL
{
class Framebuffer;
class Texture;

class Gfx final : public AbstractGfx
{
public:
  explicit Gfx(std::unique_ptr<Context> context, bool software_rasterizer_frontend = false);
  ~Gfx() override;

  bool IsHeadless() const override { return false; }
  bool SupportsUtilityDrawing() const override { return false; }

  std::unique_ptr<AbstractTexture> CreateTexture(const TextureConfig& config,
                                                 std::string_view name) override;
  std::unique_ptr<AbstractStagingTexture>
  CreateStagingTexture(StagingTextureType type, const TextureConfig& config) override;
  std::unique_ptr<AbstractFramebuffer>
  CreateFramebuffer(AbstractTexture* color_attachment, AbstractTexture* depth_attachment,
                    std::vector<AbstractTexture*> additional_color_attachments) override;

  std::unique_ptr<AbstractShader>
  CreateShaderFromSource(ShaderStage stage, std::string_view source,
                         VideoCommon::ShaderIncluder* shader_includer,
                         std::string_view name) override;
  std::unique_ptr<AbstractShader> CreateShaderFromBinary(ShaderStage stage, const void* data,
                                                         size_t length,
                                                         std::string_view name) override;
  std::unique_ptr<NativeVertexFormat>
  CreateNativeVertexFormat(const PortableVertexDeclaration& vtx_decl) override;
  std::unique_ptr<AbstractPipeline> CreatePipeline(const AbstractPipelineConfig& config,
                                                   const void* cache_data = nullptr,
                                                   size_t cache_data_length = 0) override;

  void SetPipeline(const AbstractPipeline* pipeline) override;
  void SetFramebuffer(AbstractFramebuffer* framebuffer) override;
  void SetAndDiscardFramebuffer(AbstractFramebuffer* framebuffer) override;
  void SetAndClearFramebuffer(AbstractFramebuffer* framebuffer, const ClearColor& color_value = {},
                              float depth_value = 0.0f) override;
  void ClearRegion(const MathUtil::Rectangle<int>& target_rc, bool colorEnable, bool alphaEnable,
                   bool zEnable, u32 color, u32 z) override;
  void SetScissorRect(const MathUtil::Rectangle<int>& rc) override;
  void SetTexture(u32 index, const AbstractTexture* texture) override;
  void SetSamplerState(u32 index, const SamplerState& state) override;
  void UnbindTexture(const AbstractTexture* texture) override;
  void SetViewport(float x, float y, float width, float height, float near_depth,
                   float far_depth) override;
  void Draw(u32 base_vertex, u32 num_vertices) override;
  void DrawIndexed(u32 base_index, u32 num_indices, u32 base_vertex) override;
  bool BindBackbuffer(const ClearColor& clear_color = {}) override;
  void PresentBackbuffer() override;
  void ShowImage(const AbstractTexture* source_texture,
                 const MathUtil::Rectangle<int>& source_rc) override;
  void Flush() override;
  void WaitForGPUIdle() override;

  SurfaceInfo GetSurfaceInfo() const override;

private:
  void UpdateBackbuffer();
  bool EnsureImageBlitResources(GLenum texture_target);

  std::unique_ptr<Context> m_context;
  bool m_software_rasterizer_frontend = false;
  std::unique_ptr<Framebuffer> m_system_framebuffer;
  std::array<const Texture*, VideoCommon::MAX_PIXEL_SHADER_SAMPLERS> m_bound_textures{};
  GLuint m_image_vao = 0;
  GLuint m_image_upload_texture = 0;
  u32 m_image_upload_width = 0;
  u32 m_image_upload_height = 0;
  GLuint m_image_2d_program = 0;
  GLuint m_image_2d_array_program = 0;
};
}  // namespace WebGL
