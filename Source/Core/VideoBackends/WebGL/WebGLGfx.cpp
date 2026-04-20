// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/WebGLGfx.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include <emscripten/console.h>
#include <emscripten/html5_webgl.h>

#include "Common/Logging/Log.h"

#include "VideoCommon/BPMemory.h"
#include "VideoBackends/Software/EfbCopy.h"
#include "VideoBackends/Software/Rasterizer.h"
#include "VideoBackends/Software/SWTexture.h"
#include "VideoBackends/WebGL/WebGLPipeline.h"
#include "VideoBackends/WebGL/WebGLShader.h"
#include "VideoBackends/WebGL/WebGLTexture.h"
#include "VideoBackends/WebGL/WebGLVertexFormat.h"

#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/RenderState.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/WebPerfMetrics.h"

namespace WebGL
{
extern "C" void dolphin_web_note_frame_presented(int width, int height, double copy_milliseconds,
                                                 double copy_megabytes);
extern "C" void dolphin_web_note_native_frame_presented(int width, int height);

namespace
{
float ColorComponent(u32 color, u32 shift)
{
  return static_cast<float>((color >> shift) & 0xff) / 255.0f;
}

GLenum MapFilterMode(FilterMode filter)
{
  return filter == FilterMode::Linear ? GL_LINEAR : GL_NEAREST;
}

GLenum MapWrapMode(WrapMode mode)
{
  switch (mode)
  {
  case WrapMode::Clamp:
    return GL_CLAMP_TO_EDGE;
  case WrapMode::Repeat:
    return GL_REPEAT;
  case WrapMode::Mirror:
    return GL_MIRRORED_REPEAT;
  default:
    return GL_CLAMP_TO_EDGE;
  }
}

std::string GetShaderLog(GLuint shader)
{
  GLint length = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
  if (length <= 1)
    return {};

  std::string log(static_cast<std::size_t>(length), '\0');
  GLsizei written = 0;
  glGetShaderInfoLog(shader, length, &written, log.data());
  log.resize(static_cast<std::size_t>(std::max(written, 0)));
  return log;
}

std::string GetProgramLog(GLuint program)
{
  GLint length = 0;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
  if (length <= 1)
    return {};

  std::string log(static_cast<std::size_t>(length), '\0');
  GLsizei written = 0;
  glGetProgramInfoLog(program, length, &written, log.data());
  log.resize(static_cast<std::size_t>(std::max(written, 0)));
  return log;
}

GLuint CompileShader(GLenum type, const char* source)
{
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint compile_status = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
  if (compile_status != GL_TRUE)
  {
    WARN_LOG_FMT(VIDEO, "WebGL2 image blit shader compile failed: {}", GetShaderLog(shader));
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

GLuint CreateImageBlitProgram(GLenum texture_target)
{
  static constexpr const char* vertex_source = R"(#version 300 es
precision highp float;
uniform vec4 u_src_rect;
out vec2 v_tex;

void main()
{
  vec2 pos = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
  vec2 unit = pos * 0.5 + 0.5;
  v_tex = mix(u_src_rect.xy, u_src_rect.zw, unit);
  gl_Position = vec4(pos, 0.0, 1.0);
}
)";

  static constexpr const char* fragment_2d_source = R"(#version 300 es
precision highp float;
uniform sampler2D u_image;
in vec2 v_tex;
out vec4 o_color;

void main()
{
  o_color = texture(u_image, v_tex);
}
)";

  static constexpr const char* fragment_2d_array_source = R"(#version 300 es
precision highp float;
uniform sampler2DArray u_image;
uniform float u_layer;
in vec2 v_tex;
out vec4 o_color;

void main()
{
  o_color = texture(u_image, vec3(v_tex, u_layer));
}
)";

  const GLuint vertex_shader = CompileShader(GL_VERTEX_SHADER, vertex_source);
  const GLuint fragment_shader =
      CompileShader(GL_FRAGMENT_SHADER, texture_target == GL_TEXTURE_2D_ARRAY ?
                                            fragment_2d_array_source :
                                            fragment_2d_source);
  if (vertex_shader == 0 || fragment_shader == 0)
  {
    if (vertex_shader != 0)
      glDeleteShader(vertex_shader);
    if (fragment_shader != 0)
      glDeleteShader(fragment_shader);
    return 0;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  GLint link_status = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &link_status);
  if (link_status != GL_TRUE)
  {
    WARN_LOG_FMT(VIDEO, "WebGL2 image blit program link failed: {}", GetProgramLog(program));
    glDeleteProgram(program);
    return 0;
  }

  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_image"), 0);
  const GLint layer_location = glGetUniformLocation(program, "u_layer");
  if (layer_location >= 0)
    glUniform1f(layer_location, 0.0f);
  glUseProgram(0);
  return program;
}
}  // namespace

Gfx::Gfx(std::unique_ptr<Context> context, bool software_rasterizer_frontend)
    : m_context(std::move(context)), m_software_rasterizer_frontend(software_rasterizer_frontend)
{
  m_context->MakeCurrent();
  m_context->UpdateSurfaceSize();
  m_system_framebuffer =
      Framebuffer::CreateDefault(m_context->GetBackbufferWidth(), m_context->GetBackbufferHeight());
  m_current_framebuffer = m_system_framebuffer.get();

  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glEnable(GL_SCISSOR_TEST);
  glGenVertexArrays(1, &m_attributeless_vao);
  m_current_rasterization_state = RenderState::GetInvalidRasterizationState();
  m_current_depth_state = RenderState::GetInvalidDepthState();
  m_current_blend_state = RenderState::GetInvalidBlendingState();
  UpdateActiveConfig();

  if (!m_software_rasterizer_frontend)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_context->GetBackbufferWidth(), m_context->GetBackbufferHeight());
    glScissor(0, 0, m_context->GetBackbufferWidth(), m_context->GetBackbufferHeight());
    glClearColor(0.02f, 0.08f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    PresentBackbuffer();
    emscripten_console_warn(
        "WebGL2 native GX diagnostic frame presented; waiting for game GPU frames.");
  }
}

Gfx::~Gfx()
{
  if (m_image_2d_program != 0)
    glDeleteProgram(m_image_2d_program);
  if (m_image_2d_array_program != 0)
    glDeleteProgram(m_image_2d_array_program);
  if (m_image_upload_texture != 0)
    glDeleteTextures(1, &m_image_upload_texture);
  if (m_image_vao != 0)
    glDeleteVertexArrays(1, &m_image_vao);
  if (m_attributeless_vao != 0)
    glDeleteVertexArrays(1, &m_attributeless_vao);
}

std::unique_ptr<AbstractTexture> Gfx::CreateTexture(const TextureConfig& config,
                                                    std::string_view name)
{
  if (m_software_rasterizer_frontend)
    return std::make_unique<SW::SWTexture>(config);

  return std::make_unique<Texture>(config);
}

std::unique_ptr<AbstractStagingTexture> Gfx::CreateStagingTexture(StagingTextureType type,
                                                                  const TextureConfig& config)
{
  if (m_software_rasterizer_frontend)
    return std::make_unique<SW::SWStagingTexture>(type, config);

  return std::make_unique<StagingTexture>(type, config);
}

std::unique_ptr<AbstractFramebuffer>
Gfx::CreateFramebuffer(AbstractTexture* color_attachment, AbstractTexture* depth_attachment,
                       std::vector<AbstractTexture*> additional_color_attachments)
{
  if (m_software_rasterizer_frontend)
  {
    return SW::SWFramebuffer::Create(static_cast<SW::SWTexture*>(color_attachment),
                                     static_cast<SW::SWTexture*>(depth_attachment),
                                     std::move(additional_color_attachments));
  }

  return Framebuffer::Create(static_cast<Texture*>(color_attachment),
                             static_cast<Texture*>(depth_attachment),
                             std::move(additional_color_attachments));
}

std::unique_ptr<AbstractShader>
Gfx::CreateShaderFromSource(ShaderStage stage, std::string_view source,
                            VideoCommon::ShaderIncluder* shader_includer, std::string_view name)
{
  return Shader::CreateFromSource(stage, source, name);
}

std::unique_ptr<AbstractShader> Gfx::CreateShaderFromBinary(ShaderStage stage, const void* data,
                                                            size_t length, std::string_view name)
{
  return nullptr;
}

std::unique_ptr<NativeVertexFormat>
Gfx::CreateNativeVertexFormat(const PortableVertexDeclaration& vtx_decl)
{
  if (!m_software_rasterizer_frontend)
    return std::make_unique<VertexFormat>(vtx_decl);

  return std::make_unique<NativeVertexFormat>(vtx_decl);
}

std::unique_ptr<AbstractPipeline> Gfx::CreatePipeline(const AbstractPipelineConfig& config,
                                                      const void* cache_data,
                                                      size_t cache_data_length)
{
  return Pipeline::Create(config);
}

void Gfx::ApplyRasterizationState(RasterizationState state)
{
  if (m_current_rasterization_state == state)
    return;

  if (state.cull_mode != CullMode::None)
  {
    glEnable(GL_CULL_FACE);
    glFrontFace(state.cull_mode == CullMode::Front ? GL_CCW : GL_CW);
  }
  else
  {
    glDisable(GL_CULL_FACE);
  }

  m_current_rasterization_state = state;
}

void Gfx::ApplyDepthState(DepthState state)
{
  if (m_current_depth_state == state)
    return;

  static constexpr std::array<GLenum, 8> compare_functions = {
      GL_NEVER,   GL_LESS,     GL_EQUAL,  GL_LEQUAL,
      GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};

  if (state.test_enable)
  {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(state.update_enable ? GL_TRUE : GL_FALSE);
    glDepthFunc(compare_functions[u32(state.func.Value())]);
  }
  else
  {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
  }

  m_current_depth_state = state;
}

void Gfx::ApplyBlendingState(BlendingState state)
{
  if (m_current_blend_state == state)
    return;

  static constexpr std::array<GLenum, 8> src_factors = {
      GL_ZERO,      GL_ONE,           GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
      GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
  static constexpr std::array<GLenum, 8> dst_factors = {
      GL_ZERO,      GL_ONE,           GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
      GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};

  if (state.blend_enable)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);

  glBlendEquationSeparate(state.subtract ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD,
                          state.subtract_alpha ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
  glBlendFuncSeparate(src_factors[u32(state.src_factor.Value())],
                      dst_factors[u32(state.dst_factor.Value())],
                      src_factors[u32(state.src_factor_alpha.Value())],
                      dst_factors[u32(state.dst_factor_alpha.Value())]);
  glColorMask(state.color_update ? GL_TRUE : GL_FALSE, state.color_update ? GL_TRUE : GL_FALSE,
              state.color_update ? GL_TRUE : GL_FALSE, state.alpha_update ? GL_TRUE : GL_FALSE);

  m_current_blend_state = state;
}

void Gfx::SetPipeline(const AbstractPipeline* pipeline)
{
  if (m_software_rasterizer_frontend)
    return;

  if (m_current_pipeline == pipeline)
    return;

  m_current_pipeline = pipeline;
  const auto* webgl_pipeline = static_cast<const Pipeline*>(pipeline);
  if (!webgl_pipeline || !webgl_pipeline->IsValid())
  {
    glBindVertexArray(m_attributeless_vao);
    glUseProgram(0);
    return;
  }

  ApplyRasterizationState(webgl_pipeline->m_config.rasterization_state);
  ApplyDepthState(webgl_pipeline->m_config.depth_state);
  ApplyBlendingState(webgl_pipeline->m_config.blending_state);
  glBindVertexArray(webgl_pipeline->GetVertexFormat() ? webgl_pipeline->GetVertexFormat()->GetVAO() :
                                                        m_attributeless_vao);
  glUseProgram(webgl_pipeline->GetProgram());
}

void Gfx::SetFramebuffer(AbstractFramebuffer* framebuffer)
{
  if (m_software_rasterizer_frontend)
  {
    m_current_framebuffer = framebuffer;
    return;
  }

  if (m_current_framebuffer == framebuffer)
    return;

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<Framebuffer*>(framebuffer)->GetFBO());
  m_current_framebuffer = framebuffer;
}

void Gfx::SetAndDiscardFramebuffer(AbstractFramebuffer* framebuffer)
{
  SetFramebuffer(framebuffer);
}

void Gfx::SetAndClearFramebuffer(AbstractFramebuffer* framebuffer, const ClearColor& color_value,
                                 float depth_value)
{
  if (m_software_rasterizer_frontend)
  {
    m_current_framebuffer = framebuffer;
    return;
  }

  SetFramebuffer(framebuffer);

  glDisable(GL_SCISSOR_TEST);
  GLbitfield clear_mask = 0;
  if (framebuffer->HasColorBuffer())
  {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(color_value[0], color_value[1], color_value[2], color_value[3]);
    clear_mask |= GL_COLOR_BUFFER_BIT;
  }
  if (framebuffer->HasDepthBuffer())
  {
    glDepthMask(GL_TRUE);
    glClearDepthf(depth_value);
    clear_mask |= GL_DEPTH_BUFFER_BIT;
  }
  if (clear_mask != 0)
    glClear(clear_mask);
  glEnable(GL_SCISSOR_TEST);

  if (framebuffer->HasColorBuffer())
  {
    glColorMask(m_current_blend_state.color_update ? GL_TRUE : GL_FALSE,
                m_current_blend_state.color_update ? GL_TRUE : GL_FALSE,
                m_current_blend_state.color_update ? GL_TRUE : GL_FALSE,
                m_current_blend_state.alpha_update ? GL_TRUE : GL_FALSE);
  }
  if (framebuffer->HasDepthBuffer())
    glDepthMask(m_current_depth_state.update_enable ? GL_TRUE : GL_FALSE);
}

void Gfx::ClearRegion(const MathUtil::Rectangle<int>& target_rc, bool colorEnable,
                      bool alphaEnable, bool zEnable, u32 color, u32 z)
{
  if (m_software_rasterizer_frontend)
  {
    EfbCopy::ClearEfb();
    return;
  }

  glScissor(target_rc.left, target_rc.top, target_rc.GetWidth(), target_rc.GetHeight());

  GLbitfield clear_mask = 0;
  if (colorEnable || alphaEnable)
  {
    glColorMask(colorEnable ? GL_TRUE : GL_FALSE, colorEnable ? GL_TRUE : GL_FALSE,
                colorEnable ? GL_TRUE : GL_FALSE, alphaEnable ? GL_TRUE : GL_FALSE);
    glClearColor(ColorComponent(color, 16), ColorComponent(color, 8), ColorComponent(color, 0),
                 ColorComponent(color, 24));
    clear_mask |= GL_COLOR_BUFFER_BIT;
  }
  if (zEnable)
  {
    glDepthMask(GL_TRUE);
    glClearDepthf(static_cast<float>(z) / 16777216.0f);
    clear_mask |= GL_DEPTH_BUFFER_BIT;
  }
  if (clear_mask != 0)
    glClear(clear_mask);

  if (colorEnable || alphaEnable)
  {
    glColorMask(m_current_blend_state.color_update ? GL_TRUE : GL_FALSE,
                m_current_blend_state.color_update ? GL_TRUE : GL_FALSE,
                m_current_blend_state.color_update ? GL_TRUE : GL_FALSE,
                m_current_blend_state.alpha_update ? GL_TRUE : GL_FALSE);
  }
  if (zEnable)
    glDepthMask(m_current_depth_state.update_enable ? GL_TRUE : GL_FALSE);
}

void Gfx::SetScissorRect(const MathUtil::Rectangle<int>& rc)
{
  if (m_software_rasterizer_frontend)
  {
    Rasterizer::ScissorChanged();
    return;
  }

  glScissor(rc.left, rc.top, rc.GetWidth(), rc.GetHeight());
}

void Gfx::SetTexture(u32 index, const AbstractTexture* texture)
{
  if (m_software_rasterizer_frontend)
    return;

  if (index >= m_bound_textures.size())
    return;

  const auto* webgl_texture = static_cast<const Texture*>(texture);
  glActiveTexture(GL_TEXTURE0 + index);
  if (webgl_texture)
  {
    glBindTexture(webgl_texture->GetGLTarget(), webgl_texture->GetGLTexture());
  }
  else
  {
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  }
  m_bound_textures[index] = webgl_texture;
}

void Gfx::SetSamplerState(u32 index, const SamplerState& state)
{
  if (m_software_rasterizer_frontend)
    return;

  if (index >= m_bound_textures.size() || m_bound_textures[index] == nullptr)
    return;

  const Texture* texture = m_bound_textures[index];
  glActiveTexture(GL_TEXTURE0 + index);
  glBindTexture(texture->GetGLTarget(), texture->GetGLTexture());
  glTexParameteri(texture->GetGLTarget(), GL_TEXTURE_MIN_FILTER,
                  MapFilterMode(state.tm0.min_filter));
  glTexParameteri(texture->GetGLTarget(), GL_TEXTURE_MAG_FILTER,
                  MapFilterMode(state.tm0.mag_filter));
  glTexParameteri(texture->GetGLTarget(), GL_TEXTURE_WRAP_S, MapWrapMode(state.tm0.wrap_u));
  glTexParameteri(texture->GetGLTarget(), GL_TEXTURE_WRAP_T, MapWrapMode(state.tm0.wrap_v));
}

void Gfx::UnbindTexture(const AbstractTexture* texture)
{
  if (m_software_rasterizer_frontend)
    return;

  for (u32 i = 0; i < m_bound_textures.size(); ++i)
  {
    if (m_bound_textures[i] == texture)
      SetTexture(i, nullptr);
  }
}

void Gfx::SetViewport(float x, float y, float width, float height, float near_depth,
                      float far_depth)
{
  glViewport(static_cast<GLint>(std::ceil(x)), static_cast<GLint>(std::ceil(y)),
             static_cast<GLsizei>(std::ceil(width)), static_cast<GLsizei>(std::ceil(height)));
  glDepthRangef(near_depth, far_depth);
}

void Gfx::Draw(u32 base_vertex, u32 num_vertices)
{
  if (m_software_rasterizer_frontend)
    return;

  const auto* pipeline = static_cast<const Pipeline*>(m_current_pipeline);
  if (!pipeline || !pipeline->IsValid())
    return;

  glDrawArrays(pipeline->GetPrimitive(), base_vertex, num_vertices);
  WebPerfMetrics::NoteNativeDraw(num_vertices);
  if (glGetError() != GL_NO_ERROR)
    WebPerfMetrics::NoteNativeGLError();
}

void Gfx::DrawIndexed(u32 base_index, u32 num_indices, u32 base_vertex)
{
  if (m_software_rasterizer_frontend)
    return;

  const auto* pipeline = static_cast<const Pipeline*>(m_current_pipeline);
  if (!pipeline || !pipeline->IsValid())
    return;

  glDrawElements(pipeline->GetPrimitive(), num_indices, GL_UNSIGNED_SHORT,
                 static_cast<const u16*>(nullptr) + base_index);
  WebPerfMetrics::NoteNativeDraw(num_indices);
  if (glGetError() != GL_NO_ERROR)
    WebPerfMetrics::NoteNativeGLError();
}

bool Gfx::BindBackbuffer(const ClearColor& clear_color)
{
  UpdateBackbuffer();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  m_current_framebuffer = m_system_framebuffer.get();
  glViewport(0, 0, m_context->GetBackbufferWidth(), m_context->GetBackbufferHeight());
  glScissor(0, 0, m_context->GetBackbufferWidth(), m_context->GetBackbufferHeight());
  glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  return true;
}

void Gfx::PresentBackbuffer()
{
  const auto present_start = std::chrono::steady_clock::now();
  emscripten_webgl_commit_frame();
  const auto present_end = std::chrono::steady_clock::now();
  WebPerfMetrics::NoteWebGLPresent(
      std::chrono::duration<double, std::milli>(present_end - present_start).count());

  if (!m_software_rasterizer_frontend)
  {
    dolphin_web_note_native_frame_presented(static_cast<int>(m_context->GetBackbufferWidth()),
                                            static_cast<int>(m_context->GetBackbufferHeight()));
  }
}

void Gfx::ShowImage(const AbstractTexture* source_texture, const MathUtil::Rectangle<int>& source_rc)
{
  if (m_software_rasterizer_frontend)
  {
    static bool s_logged_software_xfb = false;
    if (!s_logged_software_xfb)
    {
      emscripten_console_warn("WebGL2 backend presenting software XFB through WebGL2.");
      s_logged_software_xfb = true;
    }

    const auto* texture = static_cast<const SW::SWTexture*>(source_texture);
    if (!texture || !EnsureImageBlitResources(GL_TEXTURE_2D))
      return;

    if (m_image_upload_texture == 0)
    {
      glGenTextures(1, &m_image_upload_texture);
      glBindTexture(GL_TEXTURE_2D, m_image_upload_texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    UpdateBackbuffer();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_current_framebuffer = m_system_framebuffer.get();
    glViewport(0, 0, m_context->GetBackbufferWidth(), m_context->GetBackbufferHeight());
    glScissor(0, 0, m_context->GetBackbufferWidth(), m_context->GetBackbufferHeight());
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_image_2d_program);
    glBindVertexArray(m_image_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_image_upload_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, texture->GetConfig().width);
    if (m_image_upload_width != texture->GetConfig().width ||
        m_image_upload_height != texture->GetConfig().height)
    {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->GetConfig().width,
                   texture->GetConfig().height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      m_image_upload_width = texture->GetConfig().width;
      m_image_upload_height = texture->GetConfig().height;
    }
    const auto upload_start = std::chrono::steady_clock::now();
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture->GetConfig().width,
                    texture->GetConfig().height, GL_RGBA, GL_UNSIGNED_BYTE, texture->GetData(0, 0));
    const auto upload_end = std::chrono::steady_clock::now();
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    const float texture_width = static_cast<float>(std::max(texture->GetConfig().width, 1u));
    const float texture_height = static_cast<float>(std::max(texture->GetConfig().height, 1u));
    const float left = std::clamp(static_cast<float>(source_rc.left) / texture_width, 0.0f, 1.0f);
    const float top = std::clamp(static_cast<float>(source_rc.top) / texture_height, 0.0f, 1.0f);
    const float right =
        std::clamp(static_cast<float>(source_rc.right) / texture_width, left, 1.0f);
    const float bottom =
        std::clamp(static_cast<float>(source_rc.bottom) / texture_height, top, 1.0f);

    // The software XFB has top-left row order while WebGL samples bottom-left texture space.
    glUniform4f(glGetUniformLocation(m_image_2d_program, "u_src_rect"), left, bottom, right, top);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    PresentBackbuffer();
    const double upload_milliseconds =
        std::chrono::duration<double, std::milli>(upload_end - upload_start).count();
    const double upload_megabytes =
        static_cast<double>(static_cast<size_t>(texture->GetConfig().width) *
                            texture->GetConfig().height * 4) /
        (1024.0 * 1024.0);
    dolphin_web_note_frame_presented(static_cast<int>(source_rc.GetWidth()),
                                     static_cast<int>(source_rc.GetHeight()), upload_milliseconds,
                                     upload_megabytes);
    return;
  }

  const auto* texture = static_cast<const Texture*>(source_texture);
  if (!texture || !EnsureImageBlitResources(texture->GetGLTarget()))
    return;

  static bool s_logged_native_xfb = false;
  if (!s_logged_native_xfb)
  {
    emscripten_console_warn("WebGL2 native GX path presenting GPU texture through WebGL2.");
    s_logged_native_xfb = true;
  }

  UpdateBackbuffer();

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  m_current_framebuffer = m_system_framebuffer.get();
  glViewport(0, 0, m_context->GetBackbufferWidth(), m_context->GetBackbufferHeight());
  glScissor(0, 0, m_context->GetBackbufferWidth(), m_context->GetBackbufferHeight());
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const GLuint program =
      texture->GetGLTarget() == GL_TEXTURE_2D_ARRAY ? m_image_2d_array_program : m_image_2d_program;
  glUseProgram(program);
  glBindVertexArray(m_image_vao);

  const float texture_width = static_cast<float>(std::max(texture->GetWidth(), 1u));
  const float texture_height = static_cast<float>(std::max(texture->GetHeight(), 1u));
  const float left = std::clamp(static_cast<float>(source_rc.left) / texture_width, 0.0f, 1.0f);
  const float top = std::clamp(static_cast<float>(source_rc.top) / texture_height, 0.0f, 1.0f);
  const float right = std::clamp(static_cast<float>(source_rc.right) / texture_width, left, 1.0f);
  const float bottom =
      std::clamp(static_cast<float>(source_rc.bottom) / texture_height, top, 1.0f);

  glUniform4f(glGetUniformLocation(program, "u_src_rect"), left, top, right, bottom);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(texture->GetGLTarget(), texture->GetGLTexture());
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glBindTexture(texture->GetGLTarget(), 0);
  glBindVertexArray(0);
  glUseProgram(0);

  PresentBackbuffer();
}

void Gfx::Flush()
{
  glFlush();
}

void Gfx::WaitForGPUIdle()
{
  glFinish();
}

SurfaceInfo Gfx::GetSurfaceInfo() const
{
  return {m_context->GetBackbufferWidth(), m_context->GetBackbufferHeight(),
          m_context->GetBackbufferScale(), AbstractTextureFormat::RGBA8};
}

void Gfx::UpdateBackbuffer()
{
  m_context->MakeCurrent();
  m_context->UpdateSurfaceSize();
  m_system_framebuffer->UpdateDimensions(m_context->GetBackbufferWidth(),
                                         m_context->GetBackbufferHeight());
}

bool Gfx::EnsureImageBlitResources(GLenum texture_target)
{
  if (m_image_vao == 0)
    glGenVertexArrays(1, &m_image_vao);

  GLuint& program = texture_target == GL_TEXTURE_2D_ARRAY ? m_image_2d_array_program :
                                                            m_image_2d_program;
  if (program == 0)
    program = CreateImageBlitProgram(texture_target);

  return m_image_vao != 0 && program != 0;
}
}  // namespace WebGL
