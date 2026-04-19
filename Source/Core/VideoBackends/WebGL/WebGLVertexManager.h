// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <GLES3/gl3.h>

#include "Common/CommonTypes.h"
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

protected:
  void ResetBuffer(u32 vertex_stride) override;
  void CommitBuffer(u32 num_vertices, u32 vertex_stride, u32 num_indices, u32* out_base_vertex,
                    u32* out_base_index) override;
  void DrawCurrentBatch(u32 base_index, u32 num_indices, u32 base_vertex) override;

private:
  GLuint m_vertex_array = 0;
  GLuint m_vertex_buffer = 0;
  GLuint m_index_buffer = 0;
};
}  // namespace WebGL
