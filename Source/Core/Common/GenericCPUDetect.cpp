// Copyright 2014 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/CPUDetect.h"

#include <algorithm>
#include <thread>

CPUInfo cpu_info;

CPUInfo::CPUInfo()
{
  num_cores = std::max(static_cast<int>(std::thread::hardware_concurrency()), 1);
}

std::string CPUInfo::Summarize()
{
  return "Generic";
}
