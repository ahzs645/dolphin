// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/WebGLShader.h"

#include <array>
#include <string>

#include "Common/Logging/Log.h"

namespace WebGL
{
namespace
{
GLenum GetGLShaderType(ShaderStage stage)
{
  switch (stage)
  {
  case ShaderStage::Vertex:
    return GL_VERTEX_SHADER;
  case ShaderStage::Pixel:
    return GL_FRAGMENT_SHADER;
  default:
    return 0;
  }
}

std::string MakeWebGLSource(std::string_view source)
{
  std::string result(source);
  if (result.starts_with("#version"))
  {
    const std::size_t line_end = result.find('\n');
    result.replace(0, line_end == std::string::npos ? result.size() : line_end + 1,
                   "#version 300 es\nprecision highp float;\nprecision highp int;\n");
  }
  else
  {
    result.insert(0, "#version 300 es\nprecision highp float;\nprecision highp int;\n");
  }
  return result;
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
}  // namespace

Shader::Shader(ShaderStage stage, GLuint shader, bool valid, std::string source)
    : AbstractShader(stage), m_shader(shader), m_valid(valid), m_source(std::move(source))
{
}

Shader::~Shader()
{
  if (m_shader != 0)
    glDeleteShader(m_shader);
}

std::unique_ptr<Shader> Shader::CreateFromSource(ShaderStage stage, std::string_view source,
                                                 std::string_view name)
{
  const GLenum shader_type = GetGLShaderType(stage);
  std::string webgl_source = MakeWebGLSource(source);

  if (shader_type == 0)
  {
    WARN_LOG_FMT(VIDEO, "WebGL2 backend does not support shader stage {} for {}.",
                 static_cast<int>(stage), name);
    return std::make_unique<Shader>(stage, 0, false, std::move(webgl_source));
  }

  const GLuint shader = glCreateShader(shader_type);
  const char* source_ptr = webgl_source.c_str();
  glShaderSource(shader, 1, &source_ptr, nullptr);
  glCompileShader(shader);

  GLint compile_status = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
  if (compile_status != GL_TRUE)
  {
    WARN_LOG_FMT(VIDEO, "WebGL2 shader compile failed for {}: {}", name, GetShaderLog(shader));
    return std::make_unique<Shader>(stage, shader, false, std::move(webgl_source));
  }

  return std::make_unique<Shader>(stage, shader, true, std::move(webgl_source));
}
}  // namespace WebGL
