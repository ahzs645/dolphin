// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"

namespace WebPerfMetrics
{
void Reset();

void NoteCachedInterpreterCPUTime(double milliseconds);
void NoteSoftwareRasterizerTime(double milliseconds, u32 batches, u32 vertices);
void NoteWebGLUpload(double milliseconds, double megabytes);
void NoteWebGLPresent(double milliseconds);
void NoteXFBCopy(double milliseconds, double megabytes);
void NoteFramePresented(u32 width, u32 height);

double GetCachedInterpreterCPUMilliseconds();
u64 GetCachedInterpreterCPUSamples();
double GetSoftwareRasterizerMilliseconds();
u64 GetSoftwareRasterizerBatches();
u64 GetSoftwareRasterizerVertices();
double GetWebGLUploadMilliseconds();
double GetWebGLUploadMegabytes();
u64 GetWebGLUploads();
double GetWebGLPresentMilliseconds();
u64 GetWebGLPresents();
double GetXFBCopyMilliseconds();
double GetXFBCopyMegabytes();
u64 GetXFBCopies();
u64 GetPresentedFrames();
u32 GetLastPresentedWidth();
u32 GetLastPresentedHeight();
}  // namespace WebPerfMetrics

