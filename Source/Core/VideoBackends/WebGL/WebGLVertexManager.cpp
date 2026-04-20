// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/WebGLVertexManager.h"

#include <algorithm>
#include <cstring>

#include "Core/System.h"

#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexShaderManager.h"

namespace WebGL
{
VertexManager::VertexManager() = default;

VertexManager::~VertexManager()
{
  glDeleteBuffers(5, m_uniform_buffers);
  if (m_index_buffer != 0)
    glDeleteBuffers(1, &m_index_buffer);
  if (m_vertex_buffer != 0)
    glDeleteBuffers(1, &m_vertex_buffer);
}

bool VertexManager::Initialize()
{
  if (!VertexManagerBase::Initialize())
    return false;

  glGenBuffers(1, &m_vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, VERTEX_STREAM_BUFFER_SIZE, nullptr, GL_STREAM_DRAW);

  glGenBuffers(1, &m_index_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, INDEX_STREAM_BUFFER_SIZE, nullptr, GL_STREAM_DRAW);

  glGenBuffers(5, m_uniform_buffers);
  EnsureUniformBuffer(1, sizeof(PixelShaderConstants));
  EnsureUniformBuffer(2, sizeof(VertexShaderConstants));
  EnsureUniformBuffer(3, 16);
  EnsureUniformBuffer(4, sizeof(GeometryShaderConstants));

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  return true;
}

void VertexManager::UploadUtilityUniforms(const void* uniforms, u32 uniforms_size)
{
  InvalidateConstants();
  UploadUtilityUniformBlock(uniforms, uniforms_size);
}

bool VertexManager::UploadTexelBuffer(const void* data, u32 data_size, TexelBufferFormat format,
                                      u32* out_offset)
{
  return false;
}

bool VertexManager::UploadTexelBuffer(const void* data, u32 data_size, TexelBufferFormat format,
                                      u32* out_offset, const void* palette_data, u32 palette_size,
                                      TexelBufferFormat palette_format, u32* out_palette_offset)
{
  return false;
}

void VertexManager::ResetBuffer(u32 vertex_stride)
{
  (void)vertex_stride;

  // WebGL2 does not expose persistent mapping. Keep Dolphin's existing CPU-side staging buffers and
  // upload the committed range with glBufferSubData until a ring-buffer/orphaning strategy lands.
  m_base_buffer_pointer = m_cpu_vertex_buffer.data();
  m_cur_buffer_pointer = m_cpu_vertex_buffer.data();
  m_end_buffer_pointer = m_base_buffer_pointer + m_cpu_vertex_buffer.size();
  m_index_generator.Start(m_cpu_index_buffer.data());
}

void VertexManager::CommitBuffer(u32 num_vertices, u32 vertex_stride, u32 num_indices,
                                 u32* out_base_vertex, u32* out_base_index)
{
  const u32 vertex_data_size = num_vertices * vertex_stride;
  const u32 index_data_size = num_indices * sizeof(u16);

  glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
  if (vertex_data_size > 0)
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_data_size, m_base_buffer_pointer);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);
  if (index_data_size > 0)
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_data_size, m_cpu_index_buffer.data());

  *out_base_vertex = 0;
  *out_base_index = 0;

  ADDSTAT(g_stats.this_frame.bytes_vertex_streamed, vertex_data_size);
  ADDSTAT(g_stats.this_frame.bytes_index_streamed, index_data_size);
}

void VertexManager::UploadUniforms()
{
  auto& system = Core::System::GetInstance();
  auto& pixel_shader_manager = system.GetPixelShaderManager();
  auto& vertex_shader_manager = system.GetVertexShaderManager();
  auto& geometry_shader_manager = system.GetGeometryShaderManager();

  if (pixel_shader_manager.dirty)
  {
    EnsureUniformBuffer(1, sizeof(PixelShaderConstants));
    glBindBuffer(GL_UNIFORM_BUFFER, m_uniform_buffers[1]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PixelShaderConstants),
                    &pixel_shader_manager.constants);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_uniform_buffers[1]);
    pixel_shader_manager.dirty = false;
    ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, sizeof(PixelShaderConstants));
  }

  if (vertex_shader_manager.dirty)
  {
    EnsureUniformBuffer(2, sizeof(VertexShaderConstants));
    glBindBuffer(GL_UNIFORM_BUFFER, m_uniform_buffers[2]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(VertexShaderConstants),
                    &vertex_shader_manager.constants);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_uniform_buffers[2]);
    vertex_shader_manager.dirty = false;
    ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, sizeof(VertexShaderConstants));
  }

  if (pixel_shader_manager.custom_constants_dirty)
  {
    const u32 custom_size = static_cast<u32>(pixel_shader_manager.custom_constants.size());
    EnsureUniformBuffer(3, std::max(custom_size, 16u));
    if (custom_size > 0)
    {
      glBindBuffer(GL_UNIFORM_BUFFER, m_uniform_buffers[3]);
      glBufferSubData(GL_UNIFORM_BUFFER, 0, custom_size,
                      pixel_shader_manager.custom_constants.data());
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_uniform_buffers[3]);
    pixel_shader_manager.custom_constants_dirty = false;
    ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, custom_size);
  }

  if (geometry_shader_manager.dirty)
  {
    EnsureUniformBuffer(4, sizeof(GeometryShaderConstants));
    glBindBuffer(GL_UNIFORM_BUFFER, m_uniform_buffers[4]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(GeometryShaderConstants),
                    &geometry_shader_manager.constants);
    glBindBufferBase(GL_UNIFORM_BUFFER, 4, m_uniform_buffers[4]);
    geometry_shader_manager.dirty = false;
    ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, sizeof(GeometryShaderConstants));
  }

  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void VertexManager::DrawCurrentBatch(u32 base_index, u32 num_indices, u32 base_vertex)
{
  glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);

  VertexManagerBase::DrawCurrentBatch(base_index, num_indices, base_vertex);
}

void VertexManager::UploadUtilityUniformBlock(const void* data, u32 data_size)
{
  EnsureUniformBuffer(1, std::max(data_size, 16u));
  glBindBuffer(GL_UNIFORM_BUFFER, m_uniform_buffers[1]);
  if (data_size > 0)
    glBufferSubData(GL_UNIFORM_BUFFER, 0, data_size, data);

  for (u32 index = 1; index <= 4; ++index)
    glBindBufferBase(GL_UNIFORM_BUFFER, index, m_uniform_buffers[1]);

  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, data_size);
}

void VertexManager::EnsureUniformBuffer(u32 index, u32 size)
{
  if (index >= 5 || m_uniform_buffers[index] == 0 || m_uniform_buffer_sizes[index] >= size)
  {
    return;
  }

  glBindBuffer(GL_UNIFORM_BUFFER, m_uniform_buffers[index]);
  glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_STREAM_DRAW);
  m_uniform_buffer_sizes[index] = size;
}
}  // namespace WebGL
