// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <GLES3/gl3.h>

#include "Common/CommonTypes.h"
#include "VideoCommon/ConstantManager.h"
#include "VideoCommon/VertexManagerBase.h"

namespace WebGL
{
// Early WebGL2 GX vertex upload scaffold.
//
// This is intentionally not wired into VideoBackend yet. The current browser renderer still uses
// SWVertexLoader so the WiiFin proof-of-life remains correct while the native WebGL2 GX path grows
// behind it.
class VertexManager final : public VertexManagerBase
{
public:
  VertexManager();
  ~VertexManager() override;

  bool Initialize() override;

  void UploadUtilityUniforms(const void* uniforms, u32 uniforms_size) override;
  bool UploadTexelBuffer(const void* data, u32 data_size, TexelBufferFormat format,
                         u32* out_offset) override;
  bool UploadTexelBuffer(const void* data, u32 data_size, TexelBufferFormat format, u32* out_offset,
                         const void* palette_data, u32 palette_size,
                         TexelBufferFormat palette_format, u32* out_palette_offset) override;

  GLuint GetVertexBufferHandle() const { return m_vertex_buffer; }
  GLuint GetIndexBufferHandle() const { return m_index_buffer; }

protected:
  void ResetBuffer(u32 vertex_stride) override;
  void CommitBuffer(u32 num_vertices, u32 vertex_stride, u32 num_indices, u32* out_base_vertex,
                    u32* out_base_index) override;
  void UploadUniforms() override;
  void DrawCurrentBatch(u32 base_index, u32 num_indices, u32 base_vertex) override;

private:
  void UploadUtilityUniformBlock(const void* data, u32 data_size);
  void EnsureUniformBuffer(u32 index, u32 size);

  GLuint m_vertex_buffer = 0;
  GLuint m_index_buffer = 0;
  GLuint m_uniform_buffers[5] = {};
  u32 m_uniform_buffer_sizes[5] = {};
};
}  // namespace WebGL
