// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/WebGLTexture.h"

#include <algorithm>
#include <cstring>

#include "Common/Logging/Log.h"

namespace WebGL
{
namespace
{
bool IsDepthAttachment(AbstractTextureFormat format)
{
  return AbstractTexture::IsDepthFormat(format) || AbstractTexture::IsStencilFormat(format);
}

GLenum GetFramebufferAttachment(AbstractTextureFormat format)
{
  if (AbstractTexture::IsStencilFormat(format))
    return GL_DEPTH_STENCIL_ATTACHMENT;
  if (AbstractTexture::IsDepthFormat(format))
    return GL_DEPTH_ATTACHMENT;
  return GL_COLOR_ATTACHMENT0;
}

void AttachTexture(GLenum framebuffer_target, GLenum attachment, const Texture* texture, u32 layer,
                   u32 level)
{
  if (texture == nullptr)
  {
    glFramebufferTexture2D(framebuffer_target, attachment, GL_TEXTURE_2D, 0, 0);
    return;
  }

  if (texture->GetConfig().type == AbstractTextureType::Texture_2D)
  {
    glFramebufferTexture2D(framebuffer_target, attachment, GL_TEXTURE_2D, texture->GetGLTexture(),
                           level);
  }
  else if (texture->GetConfig().type == AbstractTextureType::Texture_CubeMap)
  {
    glFramebufferTexture2D(framebuffer_target, attachment, GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer,
                           texture->GetGLTexture(), level);
  }
  else
  {
    glFramebufferTextureLayer(framebuffer_target, attachment, texture->GetGLTexture(), level,
                              layer);
  }
}

u32 MipSize(u32 value, u32 level)
{
  return std::max(value >> level, 1u);
}
}  // namespace

Texture::Texture(const TextureConfig& config) : AbstractTexture(config)
{
  m_target = GetGLTargetForConfig(config);

  glGenTextures(1, &m_texture);
  glBindTexture(m_target, m_texture);
  glTexParameteri(m_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(m_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(m_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(m_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(m_target, GL_TEXTURE_MAX_LEVEL, std::max(config.levels, 1u) - 1);

  if (config.IsMultisampled())
  {
    WARN_LOG_FMT(VIDEO, "WebGL2 texture requested with {} samples; allocating as single-sample.",
                 config.samples);
  }

  const GLenum internal_format = GetGLInternalFormat(config.format);
  const GLenum format = GetGLFormat(config.format);
  const GLenum type = GetGLType(config.format);

  for (u32 level = 0; level < std::max(config.levels, 1u); ++level)
  {
    const u32 width = MipSize(config.width, level);
    const u32 height = MipSize(config.height, level);

    if (config.type == AbstractTextureType::Texture_2D)
    {
      glTexImage2D(m_target, level, internal_format, width, height, 0, format, type, nullptr);
    }
    else if (config.type == AbstractTextureType::Texture_CubeMap)
    {
      for (u32 layer = 0; layer < std::min(config.layers, 6u); ++layer)
      {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, level, internal_format, width, height,
                     0, format, type, nullptr);
      }
    }
    else
    {
      glTexImage3D(m_target, level, internal_format, width, height, std::max(config.layers, 1u), 0,
                   format, type, nullptr);
    }
  }
}

Texture::~Texture()
{
  if (m_texture != 0)
    glDeleteTextures(1, &m_texture);
}

GLenum Texture::GetGLTargetForConfig(const TextureConfig& config)
{
  switch (config.type)
  {
  case AbstractTextureType::Texture_2D:
    return GL_TEXTURE_2D;
  case AbstractTextureType::Texture_CubeMap:
    return GL_TEXTURE_CUBE_MAP;
  case AbstractTextureType::Texture_2DArray:
  default:
    return GL_TEXTURE_2D_ARRAY;
  }
}

GLenum Texture::GetGLInternalFormat(AbstractTextureFormat format)
{
  switch (format)
  {
  case AbstractTextureFormat::RGBA8:
  case AbstractTextureFormat::BGRA8:
    return GL_RGBA8;
  case AbstractTextureFormat::RGB10_A2:
    return GL_RGB10_A2;
  case AbstractTextureFormat::RGBA16F:
    return GL_RGBA16F;
  case AbstractTextureFormat::R16:
    return GL_R16UI;
  case AbstractTextureFormat::R32F:
    return GL_R32F;
  case AbstractTextureFormat::D16:
    return GL_DEPTH_COMPONENT16;
  case AbstractTextureFormat::D24_S8:
    return GL_DEPTH24_STENCIL8;
  case AbstractTextureFormat::D32F:
    return GL_DEPTH_COMPONENT32F;
  case AbstractTextureFormat::D32F_S8:
    return GL_DEPTH32F_STENCIL8;
  default:
    WARN_LOG_FMT(VIDEO, "Unsupported WebGL2 texture format {}; falling back to RGBA8.",
                 static_cast<u32>(format));
    return GL_RGBA8;
  }
}

GLenum Texture::GetGLFormat(AbstractTextureFormat format)
{
  switch (format)
  {
  case AbstractTextureFormat::R16:
    return GL_RED_INTEGER;
  case AbstractTextureFormat::R32F:
    return GL_RED;
  case AbstractTextureFormat::D16:
  case AbstractTextureFormat::D32F:
    return GL_DEPTH_COMPONENT;
  case AbstractTextureFormat::D24_S8:
  case AbstractTextureFormat::D32F_S8:
    return GL_DEPTH_STENCIL;
  default:
    return GL_RGBA;
  }
}

GLenum Texture::GetGLType(AbstractTextureFormat format)
{
  switch (format)
  {
  case AbstractTextureFormat::RGB10_A2:
    return GL_UNSIGNED_INT_2_10_10_10_REV;
  case AbstractTextureFormat::RGBA16F:
    return GL_HALF_FLOAT;
  case AbstractTextureFormat::R16:
    return GL_UNSIGNED_SHORT;
  case AbstractTextureFormat::R32F:
  case AbstractTextureFormat::D32F:
    return GL_FLOAT;
  case AbstractTextureFormat::D16:
    return GL_UNSIGNED_SHORT;
  case AbstractTextureFormat::D24_S8:
    return GL_UNSIGNED_INT_24_8;
  case AbstractTextureFormat::D32F_S8:
    return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
  default:
    return GL_UNSIGNED_BYTE;
  }
}

void Texture::CopyRectangleFromTexture(const AbstractTexture* src,
                                       const MathUtil::Rectangle<int>& src_rect, u32 src_layer,
                                       u32 src_level, const MathUtil::Rectangle<int>& dst_rect,
                                       u32 dst_layer, u32 dst_level)
{
  const auto* source = static_cast<const Texture*>(src);
  const GLenum attachment = GetFramebufferAttachment(source->GetFormat());
  const GLbitfield mask = IsDepthAttachment(source->GetFormat()) ? GL_DEPTH_BUFFER_BIT :
                                                            GL_COLOR_BUFFER_BIT;

  GLint previous_read_fbo = 0;
  GLint previous_draw_fbo = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);

  GLuint read_fbo = 0;
  GLuint draw_fbo = 0;
  glGenFramebuffers(1, &read_fbo);
  glGenFramebuffers(1, &draw_fbo);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
  AttachTexture(GL_READ_FRAMEBUFFER, attachment, source, src_layer, src_level);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo);
  AttachTexture(GL_DRAW_FRAMEBUFFER, attachment, this, dst_layer, dst_level);
  glDisable(GL_SCISSOR_TEST);
  glBlitFramebuffer(src_rect.left, src_rect.top, src_rect.right, src_rect.bottom, dst_rect.left,
                    dst_rect.top, dst_rect.right, dst_rect.bottom, mask, GL_NEAREST);
  glEnable(GL_SCISSOR_TEST);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, previous_read_fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previous_draw_fbo);
  glDeleteFramebuffers(1, &draw_fbo);
  glDeleteFramebuffers(1, &read_fbo);
}

void Texture::ResolveFromTexture(const AbstractTexture* src, const MathUtil::Rectangle<int>& rect,
                                 u32 layer, u32 level)
{
  CopyRectangleFromTexture(src, rect, layer, level, rect, layer, level);
}

void Texture::Load(u32 level, u32 width, u32 height, u32 row_length, const u8* buffer,
                   size_t buffer_size, u32 layer)
{
  glBindTexture(m_target, m_texture);
  if (row_length != width)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);

  const GLenum format = GetGLFormat(m_config.format);
  const GLenum type = GetGLType(m_config.format);
  if (m_config.type == AbstractTextureType::Texture_2D)
  {
    glTexSubImage2D(m_target, level, 0, 0, width, height, format, type, buffer);
  }
  else if (m_config.type == AbstractTextureType::Texture_CubeMap)
  {
    glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, level, 0, 0, width, height, format,
                    type, buffer);
  }
  else
  {
    glTexSubImage3D(m_target, level, 0, 0, layer, width, height, 1, format, type, buffer);
  }

  if (row_length != width)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

StagingTexture::StagingTexture(StagingTextureType type, const TextureConfig& config)
    : AbstractStagingTexture(type, config)
{
  m_buffer.resize(config.GetStride() * config.height);
  m_map_pointer = reinterpret_cast<char*>(m_buffer.data());
  m_map_stride = config.GetStride();
}

void StagingTexture::CopyFromTexture(const AbstractTexture* src,
                                     const MathUtil::Rectangle<int>& src_rect, u32 src_layer,
                                     u32 src_level, const MathUtil::Rectangle<int>& dst_rect)
{
  const auto* source = static_cast<const Texture*>(src);
  if (IsDepthAttachment(source->GetFormat()))
  {
    WARN_LOG_FMT(VIDEO, "WebGL2 staging depth readback is not implemented yet.");
    std::ranges::fill(m_buffer, 0);
    m_needs_flush = true;
    return;
  }

  GLint previous_read_fbo = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);

  GLuint read_fbo = 0;
  glGenFramebuffers(1, &read_fbo);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
  AttachTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, source, src_layer, src_level);

  const size_t dst_offset = dst_rect.top * m_map_stride + dst_rect.left * m_texel_size;
  glPixelStorei(GL_PACK_ROW_LENGTH, m_config.width);
  glReadPixels(src_rect.left, src_rect.top, src_rect.GetWidth(), src_rect.GetHeight(),
               Texture::GetGLFormat(source->GetFormat()), Texture::GetGLType(source->GetFormat()),
               m_buffer.data() + dst_offset);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, previous_read_fbo);
  glDeleteFramebuffers(1, &read_fbo);
  m_needs_flush = true;
}

void StagingTexture::CopyToTexture(const MathUtil::Rectangle<int>& src_rect, AbstractTexture* dst,
                                   const MathUtil::Rectangle<int>& dst_rect, u32 dst_layer,
                                   u32 dst_level)
{
  auto* target = static_cast<Texture*>(dst);
  const size_t src_offset = src_rect.top * m_map_stride + src_rect.left * m_texel_size;
  const GLenum gl_target = target->GetGLTarget();

  glBindTexture(gl_target, target->GetGLTexture());
  glPixelStorei(GL_UNPACK_ROW_LENGTH, m_config.width);

  if (target->GetConfig().type == AbstractTextureType::Texture_2D)
  {
    glTexSubImage2D(gl_target, dst_level, dst_rect.left, dst_rect.top, dst_rect.GetWidth(),
                    dst_rect.GetHeight(), Texture::GetGLFormat(target->GetFormat()),
                    Texture::GetGLType(target->GetFormat()), m_buffer.data() + src_offset);
  }
  else
  {
    glTexSubImage3D(gl_target, dst_level, dst_rect.left, dst_rect.top, dst_layer,
                    dst_rect.GetWidth(), dst_rect.GetHeight(), 1,
                    Texture::GetGLFormat(target->GetFormat()),
                    Texture::GetGLType(target->GetFormat()), m_buffer.data() + src_offset);
  }

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  m_needs_flush = true;
}

bool StagingTexture::Map()
{
  return true;
}

void StagingTexture::Unmap()
{
}

void StagingTexture::Flush()
{
  m_needs_flush = false;
}

Framebuffer::Framebuffer(AbstractTexture* color_attachment, AbstractTexture* depth_attachment,
                         std::vector<AbstractTexture*> additional_color_attachments,
                         AbstractTextureFormat color_format, AbstractTextureFormat depth_format,
                         u32 width, u32 height, u32 layers, u32 samples, GLuint fbo)
    : AbstractFramebuffer(color_attachment, depth_attachment,
                          std::move(additional_color_attachments), color_format, depth_format,
                          width, height, layers, samples),
      m_fbo(fbo)
{
}

Framebuffer::~Framebuffer()
{
  if (m_fbo != 0)
    glDeleteFramebuffers(1, &m_fbo);
}

std::unique_ptr<Framebuffer>
Framebuffer::Create(Texture* color_attachment, Texture* depth_attachment,
                    std::vector<AbstractTexture*> additional_color_attachments)
{
  if (!ValidateConfig(color_attachment, depth_attachment, additional_color_attachments))
    return nullptr;

  const AbstractTextureFormat color_format =
      color_attachment ? color_attachment->GetFormat() : AbstractTextureFormat::Undefined;
  const AbstractTextureFormat depth_format =
      depth_attachment ? depth_attachment->GetFormat() : AbstractTextureFormat::Undefined;
  const Texture* either_attachment = color_attachment ? color_attachment : depth_attachment;
  const u32 width = either_attachment->GetWidth();
  const u32 height = either_attachment->GetHeight();
  const u32 layers = either_attachment->GetLayers();
  const u32 samples = either_attachment->GetSamples();

  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);

  std::vector<GLenum> buffers;
  if (color_attachment)
  {
    AttachTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, color_attachment, 0, 0);
    buffers.push_back(GL_COLOR_ATTACHMENT0);
  }
  if (depth_attachment)
  {
    AttachTexture(GL_FRAMEBUFFER, GetFramebufferAttachment(depth_format), depth_attachment, 0, 0);
  }
  for (std::size_t i = 0; i < additional_color_attachments.size(); ++i)
  {
    auto* attachment = static_cast<Texture*>(additional_color_attachments[i]);
    const GLenum attachment_slot = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i + 1);
    AttachTexture(GL_FRAMEBUFFER, attachment_slot, attachment, 0, 0);
    buffers.push_back(attachment_slot);
  }

  if (!buffers.empty())
    glDrawBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());

  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE)
    WARN_LOG_FMT(VIDEO, "WebGL2 framebuffer is incomplete: 0x{:x}", status);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return std::make_unique<Framebuffer>(color_attachment, depth_attachment,
                                       std::move(additional_color_attachments), color_format,
                                       depth_format, width, height, layers, samples, fbo);
}

std::unique_ptr<Framebuffer> Framebuffer::CreateDefault(u32 width, u32 height)
{
  return std::make_unique<Framebuffer>(nullptr, nullptr, std::vector<AbstractTexture*>{},
                                       AbstractTextureFormat::RGBA8,
                                       AbstractTextureFormat::Undefined, width, height, 1, 1, 0);
}

void Framebuffer::UpdateDimensions(u32 width, u32 height)
{
  m_width = width;
  m_height = height;
}
}  // namespace WebGL
