// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/WebGLVertexManager.h"

#include "VideoCommon/Statistics.h"

namespace WebGL
{
VertexManager::VertexManager() = default;

VertexManager::~VertexManager()
{
  if (m_index_buffer != 0)
    glDeleteBuffers(1, &m_index_buffer);
  if (m_vertex_buffer != 0)
    glDeleteBuffers(1, &m_vertex_buffer);
  if (m_vertex_array != 0)
    glDeleteVertexArrays(1, &m_vertex_array);
}

bool VertexManager::Initialize()
{
  if (!VertexManagerBase::Initialize())
    return false;

  glGenVertexArrays(1, &m_vertex_array);
  glBindVertexArray(m_vertex_array);

  glGenBuffers(1, &m_vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, VERTEX_STREAM_BUFFER_SIZE, nullptr, GL_STREAM_DRAW);

  glGenBuffers(1, &m_index_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, INDEX_STREAM_BUFFER_SIZE, nullptr, GL_STREAM_DRAW);

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  return true;
}

void VertexManager::UploadUtilityUniforms(const void* uniforms, u32 uniforms_size)
{
  (void)uniforms;
  (void)uniforms_size;
  InvalidateConstants();
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

  glBindVertexArray(m_vertex_array);

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

void VertexManager::DrawCurrentBatch(u32 base_index, u32 num_indices, u32 base_vertex)
{
  glBindVertexArray(m_vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);

  VertexManagerBase::DrawCurrentBatch(base_index, num_indices, base_vertex);
}
}  // namespace WebGL
