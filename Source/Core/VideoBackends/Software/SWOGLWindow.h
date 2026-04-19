// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#ifdef __EMSCRIPTEN__
#include <vector>
#endif

#include "Common/CommonTypes.h"
#include "Common/MathUtil.h"

class AbstractTexture;
#ifndef __EMSCRIPTEN__
class GLContext;
#endif
struct WindowSystemInfo;

class SWOGLWindow
{
public:
  ~SWOGLWindow();

#ifdef __EMSCRIPTEN__
  u32 GetBackBufferWidth() const { return m_backbuffer_width; }
  u32 GetBackBufferHeight() const { return m_backbuffer_height; }
#else
  GLContext* GetContext() const { return m_gl_context.get(); }
#endif
  bool IsHeadless() const;

  // Image to show, will be swapped immediately
  void ShowImage(const AbstractTexture* image, const MathUtil::Rectangle<int>& xfb_region);

  static std::unique_ptr<SWOGLWindow> Create(const WindowSystemInfo& wsi);

private:
  SWOGLWindow();

  bool Initialize(const WindowSystemInfo& wsi);

#ifdef __EMSCRIPTEN__
  u32 m_backbuffer_width = 640;
  u32 m_backbuffer_height = 480;
  bool m_is_browser_surface = false;
  std::vector<u8> m_framebuffer;
#else
  u32 m_image_program = 0;
  u32 m_image_texture = 0;
  u32 m_image_vao = 0;

  std::unique_ptr<GLContext> m_gl_context;
#endif
};
