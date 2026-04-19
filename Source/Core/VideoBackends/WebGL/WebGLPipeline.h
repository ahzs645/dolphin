// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include <GLES3/gl3.h>

#include "VideoCommon/AbstractPipeline.h"

namespace WebGL
{
class Pipeline final : public AbstractPipeline
{
public:
  Pipeline(const AbstractPipelineConfig& config, GLuint program, GLenum primitive, bool valid);
  ~Pipeline() override;

  static std::unique_ptr<Pipeline> Create(const AbstractPipelineConfig& config);

  GLuint GetProgram() const { return m_program; }
  GLenum GetPrimitive() const { return m_primitive; }
  bool IsValid() const { return m_valid; }

private:
  GLuint m_program = 0;
  GLenum m_primitive = GL_TRIANGLES;
  bool m_valid = false;
};
}  // namespace WebGL
