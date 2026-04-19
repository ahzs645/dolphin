// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/WebGLContext.h"

#include <algorithm>

#include "Common/Logging/Log.h"

namespace WebGL
{
Context::Context(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE handle, float backbuffer_scale)
    : m_handle(handle), m_backbuffer_scale(backbuffer_scale)
{
  UpdateSurfaceSize();
}

Context::~Context()
{
  if (m_handle != 0)
    emscripten_webgl_destroy_context(m_handle);
}

std::unique_ptr<Context> Context::Create(const WindowSystemInfo& wsi)
{
  if (wsi.type != WindowSystemType::Web)
  {
    ERROR_LOG_FMT(VIDEO, "WebGL2 context creation requires WindowSystemType::Web.");
    return nullptr;
  }

  EmscriptenWebGLContextAttributes attributes;
  emscripten_webgl_init_context_attributes(&attributes);
  attributes.alpha = false;
  attributes.depth = true;
  attributes.stencil = true;
  attributes.antialias = false;
  attributes.premultipliedAlpha = false;
  attributes.preserveDrawingBuffer = false;
  attributes.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
  attributes.majorVersion = 2;
  attributes.minorVersion = 0;
  attributes.enableExtensionsByDefault = true;
  attributes.explicitSwapControl = true;
  attributes.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_ALWAYS;
  attributes.renderViaOffscreenBackBuffer = true;

  const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE handle =
      emscripten_webgl_create_context(WEBGL_CANVAS_SELECTOR, &attributes);
  if (handle == 0)
  {
    ERROR_LOG_FMT(VIDEO, "Failed to create a browser WebGL2 context on {}.",
                  WEBGL_CANVAS_SELECTOR);
    return nullptr;
  }

  auto context = std::unique_ptr<Context>(new Context(handle, wsi.render_surface_scale));
  if (!context->MakeCurrent())
    return nullptr;

  return context;
}

bool Context::MakeCurrent() const
{
  return emscripten_webgl_make_context_current(m_handle) == EMSCRIPTEN_RESULT_SUCCESS;
}

void Context::UpdateSurfaceSize()
{
  int width = 0;
  int height = 0;
  if (emscripten_webgl_get_drawing_buffer_size(m_handle, &width, &height) !=
      EMSCRIPTEN_RESULT_SUCCESS)
  {
    return;
  }

  m_backbuffer_width = std::max(width, 1);
  m_backbuffer_height = std::max(height, 1);
}
}  // namespace WebGL
