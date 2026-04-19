// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/WebGLPipeline.h"

#include <algorithm>
#include <string>

#include "Common/Logging/Log.h"
#include "VideoBackends/WebGL/WebGLShader.h"

namespace WebGL
{
namespace
{
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
}  // namespace

Pipeline::Pipeline(const AbstractPipelineConfig& config, GLuint program, GLenum primitive,
                   bool valid)
    : AbstractPipeline(config), m_program(program), m_primitive(primitive), m_valid(valid)
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
  const GLenum primitive = MapPrimitive(config.rasterization_state.primitive);

  if (!vertex_shader || !pixel_shader || !vertex_shader->IsValid() || !pixel_shader->IsValid())
    return std::make_unique<Pipeline>(config, 0, primitive, false);

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader->GetGLShader());
  glAttachShader(program, pixel_shader->GetGLShader());
  glLinkProgram(program);

  GLint link_status = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &link_status);
  if (link_status != GL_TRUE)
  {
    WARN_LOG_FMT(VIDEO, "WebGL2 program link failed: {}", GetProgramLog(program));
    return std::make_unique<Pipeline>(config, program, primitive, false);
  }

  return std::make_unique<Pipeline>(config, program, primitive, true);
}
}  // namespace WebGL
