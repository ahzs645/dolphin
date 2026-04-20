// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/WebGL/VideoBackend.h"

#include <memory>

#include <emscripten/console.h>

#include "Common/Common.h"
#include "Common/Logging/Log.h"
#include "Common/WindowSystemInfo.h"

#include "VideoBackends/Null/NullBoundingBox.h"
#include "VideoBackends/Null/PerfQuery.h"
#include "VideoBackends/Software/Clipper.h"
#include "VideoBackends/Software/Rasterizer.h"
#include "VideoBackends/Software/SWBoundingBox.h"
#include "VideoBackends/Software/SWEfbInterface.h"
#include "VideoBackends/Software/SWVertexLoader.h"
#include "VideoBackends/Software/TextureCache.h"
#include "VideoBackends/WebGL/WebGLContext.h"
#include "VideoBackends/WebGL/WebGLGfx.h"
#include "VideoBackends/WebGL/WebGLVertexManager.h"

#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"

namespace WebGL
{
namespace
{
bool s_experimental_gx_backend_enabled = false;
}

void SetExperimentalGXBackendEnabled(bool enabled)
{
  s_experimental_gx_backend_enabled = enabled;
}

bool IsExperimentalGXBackendEnabled()
{
  return s_experimental_gx_backend_enabled;
}

std::string VideoBackend::GetDisplayName() const
{
  return IsExperimentalGXBackendEnabled() ? _trans("WebGL2 GX experimental") : _trans("WebGL2");
}

std::optional<std::string> VideoBackend::GetWarningMessage() const
{
  if (IsExperimentalGXBackendEnabled())
  {
    return _trans("The WebGL2 GX backend is experimental. It routes Dolphin's native video path "
                  "through a browser WebGL2 context, but shader translation, framebuffer copies, "
                  "and GX state coverage are still incomplete.");
  }

  return _trans("The WebGL2 backend is currently scaffolded for browser builds. It owns a browser "
                "WebGL2 context and presentation path, but it still uses Dolphin's software "
                "rasterizer unless the experimental WebGL2 GX path is enabled.");
}

void VideoBackend::InitBackendInfo(const WindowSystemInfo& wsi)
{
  if (wsi.type != WindowSystemType::Web)
    return;

  g_backend_info.api_type = APIType::OpenGL;
  g_backend_info.MaxTextureSize = 16384;
  g_backend_info.bUsesLowerLeftOrigin = IsExperimentalGXBackendEnabled();
  g_backend_info.bSupportsExclusiveFullscreen = false;
  g_backend_info.bSupportsDualSourceBlend = false;
  g_backend_info.bSupportsPrimitiveRestart = false;
  g_backend_info.bSupportsGeometryShaders = false;
  g_backend_info.bSupportsComputeShaders = false;
  g_backend_info.bSupports3DVision = false;
  g_backend_info.bSupportsEarlyZ = false;
  g_backend_info.bSupportsBindingLayout = false;
  g_backend_info.bSupportsBBox = false;
  g_backend_info.bSupportsGSInstancing = false;
  g_backend_info.bSupportsPostProcessing = false;
  g_backend_info.bSupportsPaletteConversion = false;
  g_backend_info.bSupportsClipControl = false;
  g_backend_info.bSupportsSSAA = false;
  g_backend_info.bSupportsFragmentStoresAndAtomics = false;
  g_backend_info.bSupportsDepthClamp = false;
  // WebGL rejects glDepthRange calls where near > far, so the browser backend can't expose
  // Dolphin's reversed depth range path even when the native GX path is enabled.
  g_backend_info.bSupportsReversedDepthRange = false;
  g_backend_info.bSupportsLogicOp = false;
  g_backend_info.bSupportsMultithreading = false;
  g_backend_info.bSupportsGPUTextureDecoding = false;
  g_backend_info.bSupportsST3CTextures = false;
  g_backend_info.bSupportsCopyToVram = IsExperimentalGXBackendEnabled();
  g_backend_info.bSupportsBitfield = true;
  g_backend_info.bSupportsDynamicSamplerIndexing = false;
  g_backend_info.bSupportsBPTCTextures = false;
  g_backend_info.bSupportsFramebufferFetch = false;
  g_backend_info.bSupportsBackgroundCompiling = false;
  g_backend_info.bSupportsLargePoints = false;
  g_backend_info.bSupportsPartialDepthCopies = false;
  g_backend_info.bSupportsDepthReadback = false;
  g_backend_info.bSupportsShaderBinaries = false;
  g_backend_info.bSupportsPipelineCacheData = false;
  g_backend_info.bSupportsCoarseDerivatives = false;
  g_backend_info.bSupportsTextureQueryLevels = false;
  g_backend_info.bSupportsLodBiasInSampler = false;
  g_backend_info.bSupportsSettingObjectNames = false;
  g_backend_info.bSupportsPartialMultisampleResolve = true;
  g_backend_info.bSupportsDynamicVertexLoader = false;
  g_backend_info.bSupportsVSLinePointExpand = false;
  g_backend_info.bSupportsGLLayerInFS = false;
  g_backend_info.bSupportsHDROutput = false;
  g_backend_info.bSupportsUnrestrictedDepthRange = false;

  g_backend_info.Adapters.clear();
  g_backend_info.AdapterName =
      IsExperimentalGXBackendEnabled() ? "Browser WebGL2 GX experimental" : "Browser WebGL2";
  g_backend_info.AAModes = {1};
}

bool VideoBackend::Initialize(const WindowSystemInfo& wsi)
{
  emscripten_console_warn("WebGL2 backend Initialize entered.");
  if (wsi.type != WindowSystemType::Web)
  {
    emscripten_console_warn("WebGL2 backend rejected non-Web window system.");
    ERROR_LOG_FMT(VIDEO, "WebGL2 backend can only initialize with WindowSystemType::Web.");
    return false;
  }

  auto context = Context::Create(wsi);
  if (!context)
  {
    emscripten_console_warn("WebGL2 backend failed to create browser context.");
    return false;
  }
  emscripten_console_warn("WebGL2 backend created browser context.");

  if (IsExperimentalGXBackendEnabled())
  {
    emscripten_console_warn("WebGL2 backend using experimental native GX path.");
    return InitializeShared(std::make_unique<Gfx>(std::move(context), false),
                            std::make_unique<VertexManager>(),
                            std::make_unique<Null::PerfQuery>(),
                            std::make_unique<Null::NullBoundingBox>());
  }

  Clipper::Init();
  Rasterizer::Init();

  // This is the first visible browser-native step: the Wii/GX work still runs through Dolphin's
  // software rasterizer, but presentation now goes through this backend's WebGL2 context and
  // shader path. Replacing SWVertexLoader/SWEFBInterface is the next step toward a true GX-to-WebGL2
  // renderer.
  return InitializeShared(std::make_unique<Gfx>(std::move(context), true),
                          std::make_unique<SWVertexLoader>(), std::make_unique<Null::PerfQuery>(),
                          std::make_unique<SW::SWBoundingBox>(),
                          std::make_unique<SW::SWEFBInterface>(),
                          std::make_unique<SW::TextureCache>());
}

void VideoBackend::Shutdown()
{
  if (m_initialized)
    ShutdownShared();
}
}  // namespace WebGL
