// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <GLES3/gl3.h>

#include "VideoCommon/NativeVertexFormat.h"

namespace WebGL
{
class VertexFormat final : public NativeVertexFormat
{
public:
  explicit VertexFormat(const PortableVertexDeclaration& vtx_decl);
  ~VertexFormat() override;

  GLuint GetVAO() const { return m_vertex_array; }

private:
  GLuint m_vertex_array = 0;
};
}  // namespace WebGL
