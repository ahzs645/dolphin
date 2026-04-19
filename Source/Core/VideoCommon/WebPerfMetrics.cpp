// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/WebPerfMetrics.h"

#include <atomic>

namespace WebPerfMetrics
{
namespace
{
std::atomic<double> s_cached_interpreter_cpu_milliseconds{};
std::atomic<u64> s_cached_interpreter_cpu_samples{};
std::atomic<double> s_software_rasterizer_milliseconds{};
std::atomic<u64> s_software_rasterizer_batches{};
std::atomic<u64> s_software_rasterizer_vertices{};
std::atomic<double> s_webgl_upload_milliseconds{};
std::atomic<double> s_webgl_upload_megabytes{};
std::atomic<u64> s_webgl_uploads{};
std::atomic<double> s_webgl_present_milliseconds{};
std::atomic<u64> s_webgl_presents{};
std::atomic<double> s_xfb_copy_milliseconds{};
std::atomic<double> s_xfb_copy_megabytes{};
std::atomic<u64> s_xfb_copies{};
std::atomic<u64> s_presented_frames{};
std::atomic<u32> s_last_presented_width{};
std::atomic<u32> s_last_presented_height{};

void AddAtomicDouble(std::atomic<double>& target, double value)
{
  double current = target.load(std::memory_order_relaxed);
  while (!target.compare_exchange_weak(current, current + value, std::memory_order_relaxed,
                                       std::memory_order_relaxed))
  {
  }
}
}  // namespace

void Reset()
{
  s_cached_interpreter_cpu_milliseconds = 0.0;
  s_cached_interpreter_cpu_samples = 0;
  s_software_rasterizer_milliseconds = 0.0;
  s_software_rasterizer_batches = 0;
  s_software_rasterizer_vertices = 0;
  s_webgl_upload_milliseconds = 0.0;
  s_webgl_upload_megabytes = 0.0;
  s_webgl_uploads = 0;
  s_webgl_present_milliseconds = 0.0;
  s_webgl_presents = 0;
  s_xfb_copy_milliseconds = 0.0;
  s_xfb_copy_megabytes = 0.0;
  s_xfb_copies = 0;
  s_presented_frames = 0;
  s_last_presented_width = 0;
  s_last_presented_height = 0;
}

void NoteCachedInterpreterCPUTime(double milliseconds)
{
  AddAtomicDouble(s_cached_interpreter_cpu_milliseconds, milliseconds);
  s_cached_interpreter_cpu_samples.fetch_add(1, std::memory_order_relaxed);
}

void NoteSoftwareRasterizerTime(double milliseconds, u32 batches, u32 vertices)
{
  AddAtomicDouble(s_software_rasterizer_milliseconds, milliseconds);
  s_software_rasterizer_batches.fetch_add(batches, std::memory_order_relaxed);
  s_software_rasterizer_vertices.fetch_add(vertices, std::memory_order_relaxed);
}

void NoteWebGLUpload(double milliseconds, double megabytes)
{
  AddAtomicDouble(s_webgl_upload_milliseconds, milliseconds);
  AddAtomicDouble(s_webgl_upload_megabytes, megabytes);
  s_webgl_uploads.fetch_add(1, std::memory_order_relaxed);
}

void NoteWebGLPresent(double milliseconds)
{
  AddAtomicDouble(s_webgl_present_milliseconds, milliseconds);
  s_webgl_presents.fetch_add(1, std::memory_order_relaxed);
}

void NoteXFBCopy(double milliseconds, double megabytes)
{
  AddAtomicDouble(s_xfb_copy_milliseconds, milliseconds);
  AddAtomicDouble(s_xfb_copy_megabytes, megabytes);
  s_xfb_copies.fetch_add(1, std::memory_order_relaxed);
}

void NoteFramePresented(u32 width, u32 height)
{
  s_last_presented_width.store(width, std::memory_order_relaxed);
  s_last_presented_height.store(height, std::memory_order_relaxed);
  s_presented_frames.fetch_add(1, std::memory_order_relaxed);
}

double GetCachedInterpreterCPUMilliseconds()
{
  return s_cached_interpreter_cpu_milliseconds.load(std::memory_order_relaxed);
}

u64 GetCachedInterpreterCPUSamples()
{
  return s_cached_interpreter_cpu_samples.load(std::memory_order_relaxed);
}

double GetSoftwareRasterizerMilliseconds()
{
  return s_software_rasterizer_milliseconds.load(std::memory_order_relaxed);
}

u64 GetSoftwareRasterizerBatches()
{
  return s_software_rasterizer_batches.load(std::memory_order_relaxed);
}

u64 GetSoftwareRasterizerVertices()
{
  return s_software_rasterizer_vertices.load(std::memory_order_relaxed);
}

double GetWebGLUploadMilliseconds()
{
  return s_webgl_upload_milliseconds.load(std::memory_order_relaxed);
}

double GetWebGLUploadMegabytes()
{
  return s_webgl_upload_megabytes.load(std::memory_order_relaxed);
}

u64 GetWebGLUploads()
{
  return s_webgl_uploads.load(std::memory_order_relaxed);
}

double GetWebGLPresentMilliseconds()
{
  return s_webgl_present_milliseconds.load(std::memory_order_relaxed);
}

u64 GetWebGLPresents()
{
  return s_webgl_presents.load(std::memory_order_relaxed);
}

double GetXFBCopyMilliseconds()
{
  return s_xfb_copy_milliseconds.load(std::memory_order_relaxed);
}

double GetXFBCopyMegabytes()
{
  return s_xfb_copy_megabytes.load(std::memory_order_relaxed);
}

u64 GetXFBCopies()
{
  return s_xfb_copies.load(std::memory_order_relaxed);
}

u64 GetPresentedFrames()
{
  return s_presented_frames.load(std::memory_order_relaxed);
}

u32 GetLastPresentedWidth()
{
  return s_last_presented_width.load(std::memory_order_relaxed);
}

u32 GetLastPresentedHeight()
{
  return s_last_presented_height.load(std::memory_order_relaxed);
}
}  // namespace WebPerfMetrics

extern "C"
{
double dolphin_web_perf_cpu_milliseconds()
{
  return WebPerfMetrics::GetCachedInterpreterCPUMilliseconds();
}

int dolphin_web_perf_cpu_samples()
{
  return static_cast<int>(WebPerfMetrics::GetCachedInterpreterCPUSamples());
}

double dolphin_web_perf_sw_raster_milliseconds()
{
  return WebPerfMetrics::GetSoftwareRasterizerMilliseconds();
}

int dolphin_web_perf_sw_raster_batches()
{
  return static_cast<int>(WebPerfMetrics::GetSoftwareRasterizerBatches());
}

int dolphin_web_perf_sw_raster_vertices()
{
  return static_cast<int>(WebPerfMetrics::GetSoftwareRasterizerVertices());
}

double dolphin_web_perf_webgl_upload_milliseconds()
{
  return WebPerfMetrics::GetWebGLUploadMilliseconds();
}

double dolphin_web_perf_webgl_upload_megabytes()
{
  return WebPerfMetrics::GetWebGLUploadMegabytes();
}

int dolphin_web_perf_webgl_uploads()
{
  return static_cast<int>(WebPerfMetrics::GetWebGLUploads());
}

double dolphin_web_perf_webgl_present_milliseconds()
{
  return WebPerfMetrics::GetWebGLPresentMilliseconds();
}

int dolphin_web_perf_webgl_presents()
{
  return static_cast<int>(WebPerfMetrics::GetWebGLPresents());
}

double dolphin_web_perf_xfb_copy_milliseconds()
{
  return WebPerfMetrics::GetXFBCopyMilliseconds();
}

double dolphin_web_perf_xfb_copy_megabytes()
{
  return WebPerfMetrics::GetXFBCopyMegabytes();
}

int dolphin_web_perf_xfb_copies()
{
  return static_cast<int>(WebPerfMetrics::GetXFBCopies());
}

int dolphin_web_perf_presented_frames()
{
  return static_cast<int>(WebPerfMetrics::GetPresentedFrames());
}

int dolphin_web_perf_presented_width()
{
  return static_cast<int>(WebPerfMetrics::GetLastPresentedWidth());
}

int dolphin_web_perf_presented_height()
{
  return static_cast<int>(WebPerfMetrics::GetLastPresentedHeight());
}
}

