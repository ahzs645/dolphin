// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/WebGLShader.h"

#include <array>
#include <algorithm>
#include <string>
#include <string_view>

#include <emscripten/console.h>

#include "Common/Logging/Log.h"

namespace WebGL
{
namespace
{
constexpr int MAX_BROWSER_SHADER_WARNINGS = 8;
int s_browser_shader_warnings = 0;

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

bool IsIdentifierChar(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool LooksLikeFloatLiteralSuffix(const std::string& source, std::size_t suffix_pos)
{
  std::size_t literal_start = suffix_pos;
  while (literal_start > 0)
  {
    const char previous = source[literal_start - 1];
    if ((previous >= '0' && previous <= '9') || previous == '.' || previous == 'e' ||
        previous == 'E' || previous == '+' || previous == '-')
    {
      --literal_start;
      continue;
    }
    break;
  }

  const std::string_view literal(source.data() + literal_start, suffix_pos - literal_start);
  return literal.find('.') != std::string_view::npos || literal.find('e') != std::string_view::npos ||
         literal.find('E') != std::string_view::npos;
}

void StripFloatLiteralSuffixes(std::string* source)
{
  for (std::size_t i = 1; i < source->size(); ++i)
  {
    const char c = (*source)[i];
    if (c != 'f' && c != 'F')
      continue;

    const char previous = (*source)[i - 1];
    const char next = i + 1 < source->size() ? (*source)[i + 1] : '\0';
    if ((previous >= '0' && previous <= '9') && !IsIdentifierChar(next) &&
        LooksLikeFloatLiteralSuffix(*source, i))
    {
      source->erase(i, 1);
      --i;
    }
  }
}

std::string MakeWebGLSource(std::string_view source)
{
  static constexpr std::string_view header = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;

#define API_OPENGL 1
#define ATTRIBUTE_LOCATION(x) layout(location = x)
#define FRAGMENT_OUTPUT_LOCATION(x) layout(location = x)
#define FRAGMENT_OUTPUT_LOCATION_INDEXED(x, y) layout(location = x)
#define UBO_BINDING(packing, x) layout(packing)
#define SAMPLER_BINDING(x)
#define TEXEL_BUFFER_BINDING(x)
#define SSBO_BINDING(x) layout(std430)
#define IMAGE_BINDING(format, x) layout(format)
#define VARYING_LOCATION(x)

#define float2 vec2
#define float3 vec3
#define float4 vec4
#define uint2 uvec2
#define uint3 uvec3
#define uint4 uvec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define frac fract
#define lerp mix
#define dolphin_isnan(f) isnan(f)
#define FORCE_EARLY_Z const int dolphin_force_early_z_marker = 0

uint dolphin_bitfieldExtract(uint value, int offset, int bits)
{
  if (bits <= 0)
    return 0u;

  uint shifted = value >> uint(offset);
  if (bits >= 32)
    return shifted;

  return shifted & ((1u << uint(bits)) - 1u);
}

int dolphin_bitfieldExtract(int value, int offset, int bits)
{
  if (bits <= 0)
    return 0;

  uint extracted = dolphin_bitfieldExtract(uint(value), offset, bits);
  if (bits < 32)
  {
    uint sign_bit = 1u << uint(bits - 1);
    if ((extracted & sign_bit) != 0u)
      extracted |= ~((1u << uint(bits)) - 1u);
  }
  return int(extracted);
}

#define bitfieldExtract(value, offset, bits) dolphin_bitfieldExtract(value, int(offset), int(bits))
)";

  std::string result(source);
  if (result.starts_with("#version"))
  {
    const std::size_t line_end = result.find('\n');
    result.replace(0, line_end == std::string::npos ? result.size() : line_end + 1,
                   header);
  }
  else
  {
    result.insert(0, header);
  }
  StripFloatLiteralSuffixes(&result);
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

int GetFirstErrorLine(std::string_view log)
{
  constexpr std::string_view marker = "ERROR: 0:";
  const std::size_t marker_pos = log.find(marker);
  if (marker_pos == std::string_view::npos)
    return -1;

  std::size_t line_start = marker_pos + marker.size();
  std::size_t line_end = line_start;
  while (line_end < log.size() && log[line_end] >= '0' && log[line_end] <= '9')
    ++line_end;

  if (line_end == line_start)
    return -1;

  int line = 0;
  for (std::size_t i = line_start; i < line_end; ++i)
    line = line * 10 + (log[i] - '0');
  return line;
}

std::string GetSourceExcerpt(std::string_view source, int center_line)
{
  if (center_line <= 0)
    return {};

  const int first_line = std::max(1, center_line - 5);
  const int last_line = center_line + 5;

  std::string excerpt = "\nSource excerpt:";
  std::size_t line_offset = 0;
  int line_number = 1;

  while (line_offset < source.size() && line_number <= last_line)
  {
    const std::size_t next_line = source.find('\n', line_offset);
    const std::size_t line_end = next_line == std::string_view::npos ? source.size() : next_line;

    if (line_number >= first_line)
    {
      std::string_view line = source.substr(line_offset, line_end - line_offset);
      excerpt += "\n";
      excerpt += std::to_string(line_number);
      excerpt += line_number == center_line ? " > " : "   ";

      if (line.size() > 180)
      {
        excerpt.append(line.substr(0, 180));
        excerpt += "...";
      }
      else
      {
        excerpt.append(line);
      }
    }

    if (next_line == std::string_view::npos)
      break;

    line_offset = next_line + 1;
    ++line_number;
  }

  return excerpt;
}

void BrowserWarnShaderFailure(std::string_view name, std::string_view log,
                              std::string_view source = {})
{
  if (s_browser_shader_warnings >= MAX_BROWSER_SHADER_WARNINGS)
    return;

  ++s_browser_shader_warnings;
  std::string message = "WebGL2 shader compile failed";
  if (!name.empty())
  {
    message += " for ";
    message += name;
  }
  if (!log.empty())
  {
    message += ": ";
    message += log;
  }
  if (!source.empty())
  {
    const std::string excerpt = GetSourceExcerpt(source, GetFirstErrorLine(log));
    if (!excerpt.empty())
    {
      std::string excerpt_message = "WebGL2 failing shader source";
      if (!name.empty())
      {
        excerpt_message += " for ";
        excerpt_message += name;
      }
      excerpt_message += excerpt;
      if (excerpt_message.size() > 2000)
        excerpt_message.resize(2000);
      emscripten_console_warn(excerpt_message.c_str());
    }
  }
  if (message.size() > 1200)
    message.resize(1200);
  emscripten_console_warn(message.c_str());
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
    BrowserWarnShaderFailure(name, "unsupported shader stage");
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
    const std::string log = GetShaderLog(shader);
    WARN_LOG_FMT(VIDEO, "WebGL2 shader compile failed for {}: {}", name, log);
    BrowserWarnShaderFailure(name, log, webgl_source);
    return std::make_unique<Shader>(stage, shader, false, std::move(webgl_source));
  }

  return std::make_unique<Shader>(stage, shader, true, std::move(webgl_source));
}
}  // namespace WebGL
