// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include <emscripten/html5_webgl.h>

#include "Common/CommonTypes.h"
#include "Common/WindowSystemInfo.h"

namespace WebGL
{
constexpr const char* WEBGL_CANVAS_SELECTOR = "#dolphin-webgl-canvas";

class Context final
{
public:
  ~Context();

  static std::unique_ptr<Context> Create(const WindowSystemInfo& wsi);

  bool MakeCurrent() const;
  void UpdateSurfaceSize();

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE GetHandle() const { return m_handle; }
  u32 GetBackbufferWidth() const { return m_backbuffer_width; }
  u32 GetBackbufferHeight() const { return m_backbuffer_height; }
  float GetBackbufferScale() const { return m_backbuffer_scale; }

private:
  Context(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE handle, float backbuffer_scale);

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE m_handle = 0;
  u32 m_backbuffer_width = 640;
  u32 m_backbuffer_height = 480;
  float m_backbuffer_scale = 1.0f;
};
}  // namespace WebGL
