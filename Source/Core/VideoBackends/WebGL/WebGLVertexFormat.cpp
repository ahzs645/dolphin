// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/WebGLVertexFormat.h"

#include <cstdint>

#include "Common/EnumMap.h"

#include "VideoBackends/WebGL/WebGLVertexManager.h"

#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VertexShaderGen.h"

namespace WebGL
{
namespace
{
GLenum ComponentFormatToGL(ComponentFormat format)
{
  static constexpr Common::EnumMap<GLenum, ComponentFormat::InvalidFloat7> lookup = {
      GL_UNSIGNED_BYTE, GL_BYTE,  GL_UNSIGNED_SHORT, GL_SHORT,
      GL_FLOAT,         GL_FLOAT, GL_FLOAT,          GL_FLOAT,
  };
  return lookup[format];
}

void SetPointer(ShaderAttrib attrib, u32 stride, const AttributeFormat& format)
{
  if (!format.enable)
    return;

  const GLuint location = static_cast<GLuint>(attrib);
  glEnableVertexAttribArray(location);
  if (format.integer)
  {
    glVertexAttribIPointer(location, format.components, ComponentFormatToGL(format.type), stride,
                           reinterpret_cast<const void*>(static_cast<uintptr_t>(format.offset)));
  }
  else
  {
    glVertexAttribPointer(location, format.components, ComponentFormatToGL(format.type), GL_TRUE,
                          stride,
                          reinterpret_cast<const void*>(static_cast<uintptr_t>(format.offset)));
  }
}
}  // namespace

VertexFormat::VertexFormat(const PortableVertexDeclaration& vtx_decl)
    : NativeVertexFormat(vtx_decl)
{
  const auto* vertex_manager = static_cast<const VertexManager*>(g_vertex_manager.get());

  glGenVertexArrays(1, &m_vertex_array);
  glBindVertexArray(m_vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_manager->GetVertexBufferHandle());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertex_manager->GetIndexBufferHandle());

  const u32 vertex_stride = static_cast<u32>(vtx_decl.stride);
  SetPointer(ShaderAttrib::Position, vertex_stride, vtx_decl.position);

  for (u32 i = 0; i < 3; ++i)
    SetPointer(ShaderAttrib::Normal + i, vertex_stride, vtx_decl.normals[i]);

  for (u32 i = 0; i < 2; ++i)
    SetPointer(ShaderAttrib::Color0 + i, vertex_stride, vtx_decl.colors[i]);

  for (u32 i = 0; i < 8; ++i)
    SetPointer(ShaderAttrib::TexCoord0 + i, vertex_stride, vtx_decl.texcoords[i]);

  SetPointer(ShaderAttrib::PositionMatrix, vertex_stride, vtx_decl.posmtx);

  glBindVertexArray(0);
}

VertexFormat::~VertexFormat()
{
  if (m_vertex_array != 0)
    glDeleteVertexArrays(1, &m_vertex_array);
}
}  // namespace WebGL
