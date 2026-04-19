// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

#include <GLES3/gl3.h>

#include "VideoCommon/AbstractShader.h"

namespace WebGL
{
class Shader final : public AbstractShader
{
public:
  Shader(ShaderStage stage, GLuint shader, bool valid, std::string source);
  ~Shader() override;

  static std::unique_ptr<Shader> CreateFromSource(ShaderStage stage, std::string_view source,
                                                  std::string_view name);

  GLuint GetGLShader() const { return m_shader; }
  bool IsValid() const { return m_valid; }

private:
  GLuint m_shader = 0;
  bool m_valid = false;
  std::string m_source;
};
}  // namespace WebGL
