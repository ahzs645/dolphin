// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/Software/SWOGLWindow.h"

#ifdef __EMSCRIPTEN__
#include <algorithm>
#include <cstring>

#include "Common/WindowSystemInfo.h"
#else
#include <memory>

#include "Common/GL/GLContext.h"
#include "Common/GL/GLUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#endif

#include "VideoBackends/Software/SWTexture.h"

#ifdef __EMSCRIPTEN__
namespace
{
// Browser presentation bridge for the software renderer. The React host reads this
// buffer from WASM memory and paints it into an HTML canvas.
const u8* s_latest_frame = nullptr;
u32 s_latest_width = 0;
u32 s_latest_height = 0;
u32 s_latest_stride = 0;
u32 s_latest_version = 0;
}

extern "C"
{
int dolphin_web_frame_width()
{
  return static_cast<int>(s_latest_width);
}

int dolphin_web_frame_height()
{
  return static_cast<int>(s_latest_height);
}

int dolphin_web_frame_stride()
{
  return static_cast<int>(s_latest_stride);
}

int dolphin_web_frame_version()
{
  return static_cast<int>(s_latest_version);
}

int dolphin_web_has_frame()
{
  return s_latest_frame ? 1 : 0;
}

const unsigned char* dolphin_web_frame_buffer()
{
  return s_latest_frame;
}
}
#endif

SWOGLWindow::SWOGLWindow() = default;
SWOGLWindow::~SWOGLWindow()
{
#ifdef __EMSCRIPTEN__
  if (s_latest_frame == m_framebuffer.data())
  {
    s_latest_frame = nullptr;
    s_latest_width = 0;
    s_latest_height = 0;
    s_latest_stride = 0;
  }
#endif
}

std::unique_ptr<SWOGLWindow> SWOGLWindow::Create(const WindowSystemInfo& wsi)
{
  std::unique_ptr<SWOGLWindow> window = std::unique_ptr<SWOGLWindow>(new SWOGLWindow());
  if (!window->Initialize(wsi))
  {
#ifndef __EMSCRIPTEN__
    PanicAlertFmt("Failed to create OpenGL window");
#endif
    return nullptr;
  }

  return window;
}

bool SWOGLWindow::IsHeadless() const
{
#ifdef __EMSCRIPTEN__
  return !m_is_browser_surface;
#else
  return m_gl_context->IsHeadless();
#endif
}

bool SWOGLWindow::Initialize(const WindowSystemInfo& wsi)
{
#ifdef __EMSCRIPTEN__
  m_is_browser_surface = wsi.type == WindowSystemType::Web;
  m_backbuffer_width = 640;
  m_backbuffer_height = 480;
  return m_is_browser_surface || wsi.type == WindowSystemType::Headless;
#else
  m_gl_context = GLContext::Create(wsi);
  if (!m_gl_context)
    return false;

  // Init extension support.
  if (!GLExtensions::Init(m_gl_context.get()))
  {
    ERROR_LOG_FMT(VIDEO, "GLExtensions::Init failed!Does your video card support OpenGL 2.0?");
    return false;
  }
  else if (GLExtensions::Version() < 310)
  {
    ERROR_LOG_FMT(VIDEO, "OpenGL Version {} detected, but at least 3.1 is required.",
                  GLExtensions::Version());
    return false;
  }

  std::string frag_shader = "in vec2 TexCoord;\n"
                            "out vec4 ColorOut;\n"
                            "uniform sampler2D samp;\n"
                            "void main() {\n"
                            "	ColorOut = texture(samp, TexCoord);\n"
                            "}\n";

  std::string vertex_shader = "out vec2 TexCoord;\n"
                              "void main() {\n"
                              "	vec2 rawpos = vec2(gl_VertexID & 1, (gl_VertexID & 2) >> 1);\n"
                              "	gl_Position = vec4(rawpos * 2.0 - 1.0, 0.0, 1.0);\n"
                              "	TexCoord = vec2(rawpos.x, -rawpos.y);\n"
                              "}\n";

  std::string header = m_gl_context->IsGLES() ? "#version 300 es\n"
                                                "precision highp float;\n" :
                                                "#version 140\n";

  m_image_program = GLUtil::CompileProgram(header + vertex_shader, header + frag_shader);

  glUseProgram(m_image_program);

  glUniform1i(glGetUniformLocation(m_image_program, "samp"), 0);
  glGenTextures(1, &m_image_texture);
  glBindTexture(GL_TEXTURE_2D, m_image_texture);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  glGenVertexArrays(1, &m_image_vao);
  return true;
#endif
}

void SWOGLWindow::ShowImage(const AbstractTexture* image,
                            const MathUtil::Rectangle<int>& xfb_region)
{
  const SW::SWTexture* sw_image = static_cast<const SW::SWTexture*>(image);
#ifdef __EMSCRIPTEN__
  const u32 source_width = sw_image->GetConfig().width;
  const u32 source_height = sw_image->GetConfig().height;
  const u32 left = std::clamp(xfb_region.left, 0, static_cast<int>(source_width));
  const u32 top = std::clamp(xfb_region.top, 0, static_cast<int>(source_height));
  const u32 right =
      std::clamp(xfb_region.right, static_cast<int>(left), static_cast<int>(source_width));
  const u32 bottom =
      std::clamp(xfb_region.bottom, static_cast<int>(top), static_cast<int>(source_height));
  const u32 width = std::max(right - left, 1u);
  const u32 height = std::max(bottom - top, 1u);
  const u32 source_stride = source_width * 4;
  const u32 target_stride = width * 4;
  const u8* source = sw_image->GetData(0, 0);

  m_framebuffer.resize(static_cast<size_t>(target_stride) * height);
  for (u32 y = 0; y < height; ++y)
  {
    std::memcpy(&m_framebuffer[static_cast<size_t>(y) * target_stride],
                source + static_cast<size_t>(top + y) * source_stride + left * 4, target_stride);
  }

  s_latest_frame = m_framebuffer.data();
  s_latest_width = width;
  s_latest_height = height;
  s_latest_stride = target_stride;
  ++s_latest_version;
  m_backbuffer_width = width;
  m_backbuffer_height = height;
#else
  m_gl_context->Update();  // just updates the render window position and the backbuffer size

  GLsizei glWidth = (GLsizei)m_gl_context->GetBackBufferWidth();
  GLsizei glHeight = (GLsizei)m_gl_context->GetBackBufferHeight();

  glViewport(0, 0, glWidth, glHeight);

  glActiveTexture(GL_TEXTURE9);
  glBindTexture(GL_TEXTURE_2D, m_image_texture);

  // TODO: Apply xfb_region

  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // 4-byte pixel alignment
  glPixelStorei(GL_UNPACK_ROW_LENGTH, sw_image->GetConfig().width);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(sw_image->GetConfig().width),
               static_cast<GLsizei>(sw_image->GetConfig().height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
               sw_image->GetData(0, 0));

  glUseProgram(m_image_program);

  glBindVertexArray(m_image_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  m_gl_context->Swap();
#endif
}
