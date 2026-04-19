// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>

#include <GLES3/gl3.h>

#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractStagingTexture.h"
#include "VideoCommon/AbstractTexture.h"

namespace WebGL
{
class Texture final : public AbstractTexture
{
public:
  explicit Texture(const TextureConfig& config);
  ~Texture() override;

  void CopyRectangleFromTexture(const AbstractTexture* src,
                                const MathUtil::Rectangle<int>& src_rect, u32 src_layer,
                                u32 src_level, const MathUtil::Rectangle<int>& dst_rect,
                                u32 dst_layer, u32 dst_level) override;
  void ResolveFromTexture(const AbstractTexture* src, const MathUtil::Rectangle<int>& rect,
                          u32 layer, u32 level) override;
  void Load(u32 level, u32 width, u32 height, u32 row_length, const u8* buffer, size_t buffer_size,
            u32 layer) override;

  GLuint GetGLTexture() const { return m_texture; }
  GLenum GetGLTarget() const { return m_target; }

  static GLenum GetGLTargetForConfig(const TextureConfig& config);
  static GLenum GetGLInternalFormat(AbstractTextureFormat format);
  static GLenum GetGLFormat(AbstractTextureFormat format);
  static GLenum GetGLType(AbstractTextureFormat format);

private:
  GLuint m_texture = 0;
  GLenum m_target = GL_TEXTURE_2D_ARRAY;
};

class StagingTexture final : public AbstractStagingTexture
{
public:
  explicit StagingTexture(StagingTextureType type, const TextureConfig& config);

  void CopyFromTexture(const AbstractTexture* src, const MathUtil::Rectangle<int>& src_rect,
                       u32 src_layer, u32 src_level,
                       const MathUtil::Rectangle<int>& dst_rect) override;
  void CopyToTexture(const MathUtil::Rectangle<int>& src_rect, AbstractTexture* dst,
                     const MathUtil::Rectangle<int>& dst_rect, u32 dst_layer,
                     u32 dst_level) override;

  bool Map() override;
  void Unmap() override;
  void Flush() override;

private:
  std::vector<u8> m_buffer;
};

class Framebuffer final : public AbstractFramebuffer
{
public:
  Framebuffer(AbstractTexture* color_attachment, AbstractTexture* depth_attachment,
              std::vector<AbstractTexture*> additional_color_attachments,
              AbstractTextureFormat color_format, AbstractTextureFormat depth_format, u32 width,
              u32 height, u32 layers, u32 samples, GLuint fbo);
  ~Framebuffer() override;

  static std::unique_ptr<Framebuffer>
  Create(Texture* color_attachment, Texture* depth_attachment,
         std::vector<AbstractTexture*> additional_color_attachments);
  static std::unique_ptr<Framebuffer> CreateDefault(u32 width, u32 height);

  GLuint GetFBO() const { return m_fbo; }
  void UpdateDimensions(u32 width, u32 height);

private:
  GLuint m_fbo = 0;
};
}  // namespace WebGL
