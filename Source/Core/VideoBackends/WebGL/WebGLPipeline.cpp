// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/WebGLPipeline.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

#include <emscripten/console.h>

#include "Common/Logging/Log.h"
#include "VideoBackends/WebGL/WebGLShader.h"
#include "VideoBackends/WebGL/WebGLVertexFormat.h"
#include "VideoCommon/VertexShaderGen.h"

namespace WebGL
{
namespace
{
constexpr int MAX_BROWSER_PIPELINE_WARNINGS = 8;
int s_browser_pipeline_warnings = 0;

GLenum MapPrimitive(PrimitiveType primitive_type)
{
  switch (primitive_type)
  {
  case PrimitiveType::Points:
    return GL_POINTS;
  case PrimitiveType::Lines:
    return GL_LINES;
  case PrimitiveType::Triangles:
    return GL_TRIANGLES;
  case PrimitiveType::TriangleStrip:
    return GL_TRIANGLE_STRIP;
  default:
    return GL_TRIANGLES;
  }
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

void BindAttributeLocations(GLuint program)
{
  glBindAttribLocation(program, static_cast<GLuint>(ShaderAttrib::Position), "rawpos");
  glBindAttribLocation(program, static_cast<GLuint>(ShaderAttrib::PositionMatrix), "posmtx");
  glBindAttribLocation(program, static_cast<GLuint>(ShaderAttrib::Color0), "rawcolor0");
  glBindAttribLocation(program, static_cast<GLuint>(ShaderAttrib::Color1), "rawcolor1");
  glBindAttribLocation(program, static_cast<GLuint>(ShaderAttrib::Normal), "rawnormal");
  glBindAttribLocation(program, static_cast<GLuint>(ShaderAttrib::Tangent), "rawtangent");
  glBindAttribLocation(program, static_cast<GLuint>(ShaderAttrib::Binormal), "rawbinormal");

  static constexpr std::array<const char*, 8> texcoord_names = {
      "rawtex0", "rawtex1", "rawtex2", "rawtex3", "rawtex4", "rawtex5", "rawtex6", "rawtex7"};
  for (u32 i = 0; i < texcoord_names.size(); ++i)
    glBindAttribLocation(program, static_cast<GLuint>(ShaderAttrib::TexCoord0 + i),
                         texcoord_names[i]);
}

void BindProgramVariables(GLuint program)
{
  glUseProgram(program);

  const std::array<std::pair<const char*, GLuint>, 6> uniform_blocks = {{
      {"PSBlock", 1},
      {"UBO", 1},
      {"VSBlock", 2},
      {"CustomShaderBlock", 3},
      {"GSBlock", 4},
      {"UBERBlock", 5},
  }};

  for (const auto& [name, binding] : uniform_blocks)
  {
    const GLuint block_index = glGetUniformBlockIndex(program, name);
    if (block_index != GL_INVALID_INDEX)
      glUniformBlockBinding(program, block_index, binding);
  }

  static constexpr std::array<GLint, 8> sampler_units = {{0, 1, 2, 3, 4, 5, 6, 7}};
  const GLint sampler_array_location = glGetUniformLocation(program, "samp[0]");
  if (sampler_array_location >= 0)
    glUniform1iv(sampler_array_location, static_cast<GLsizei>(sampler_units.size()),
                 sampler_units.data());

  for (u32 i = 0; i < sampler_units.size(); ++i)
  {
    std::string sampler_name = "samp[" + std::to_string(i) + "]";
    GLint location = glGetUniformLocation(program, sampler_name.c_str());
    if (location < 0)
    {
      sampler_name = "samp" + std::to_string(i);
      location = glGetUniformLocation(program, sampler_name.c_str());
    }
    if (location >= 0)
      glUniform1i(location, static_cast<GLint>(i));
  }

  glUseProgram(0);
}

void BrowserWarnPipelineFailure(std::string_view reason)
{
  if (s_browser_pipeline_warnings >= MAX_BROWSER_PIPELINE_WARNINGS)
    return;

  ++s_browser_pipeline_warnings;
  std::string message = "WebGL2 pipeline creation failed";
  if (!reason.empty())
  {
    message += ": ";
    message += reason;
  }
  if (message.size() > 1200)
    message.resize(1200);
  emscripten_console_warn(message.c_str());
}
}  // namespace

Pipeline::Pipeline(const AbstractPipelineConfig& config, GLuint program, GLenum primitive,
                   const VertexFormat* vertex_format, bool valid)
    : AbstractPipeline(config), m_program(program), m_primitive(primitive),
      m_vertex_format(vertex_format), m_valid(valid)
{
}

Pipeline::~Pipeline()
{
  if (m_program != 0)
    glDeleteProgram(m_program);
}

std::unique_ptr<Pipeline> Pipeline::Create(const AbstractPipelineConfig& config)
{
  const auto* vertex_shader = static_cast<const Shader*>(config.vertex_shader);
  const auto* pixel_shader = static_cast<const Shader*>(config.pixel_shader);
  const auto* vertex_format = static_cast<const VertexFormat*>(config.vertex_format);
  const GLenum primitive = MapPrimitive(config.rasterization_state.primitive);

  if (!vertex_shader || !pixel_shader || !vertex_shader->IsValid() || !pixel_shader->IsValid())
  {
    BrowserWarnPipelineFailure("missing or invalid vertex/pixel shader");
    return std::make_unique<Pipeline>(config, 0, primitive, vertex_format, false);
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader->GetGLShader());
  glAttachShader(program, pixel_shader->GetGLShader());
  BindAttributeLocations(program);
  glLinkProgram(program);

  GLint link_status = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &link_status);
  if (link_status != GL_TRUE)
  {
    const std::string log = GetProgramLog(program);
    WARN_LOG_FMT(VIDEO, "WebGL2 program link failed: {}", log);
    BrowserWarnPipelineFailure(log);
    return std::make_unique<Pipeline>(config, program, primitive, vertex_format, false);
  }

  BindProgramVariables(program);
  return std::make_unique<Pipeline>(config, program, primitive, vertex_format, true);
}
}  // namespace WebGL
