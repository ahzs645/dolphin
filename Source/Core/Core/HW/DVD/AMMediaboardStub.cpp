// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/DVD/AMMediaboard.h"

namespace AMMediaboard
{
MediaBoardRange::MediaBoardRange(u32 start_, u32 size_, std::span<u8> buffer_)
    : start(start_), end(start_ + size_), buffer(buffer_.data()), buffer_size(buffer_.size())
{
}

void Init()
{
}

void FirmwareMap(bool on)
{
}

void InitDIMM(const DiscIO::Volume& volume)
{
}

void InitKeys(u32 key_a, u32 key_b, u32 key_c)
{
}

u32 ExecuteCommand(std::array<u32, 3>& dicmd_buf, u32* diimm_buf, u32 address, u32 length)
{
  return 0;
}

u32 GetGameType()
{
  return 0;
}

u32 GetMediaType()
{
  return 0;
}

bool GetTestMenu()
{
  return false;
}

void Shutdown()
{
}

void DoState(PointerWrap& p)
{
}

std::optional<ParsedIPRedirection> ParseIPRedirection(std::string_view str)
{
  return std::nullopt;
}

Common::IPv4Port IPRedirection::Apply(Common::IPv4Port subject) const
{
  return subject;
}

Common::IPv4Port IPRedirection::Reverse(Common::IPv4Port subject) const
{
  return subject;
}

std::string IPRedirection::ToString() const
{
  return {};
}

IPRedirections GetIPRedirections()
{
  return {};
}

s32 DebuggerGetSocket(u32 triforce_fd)
{
  return -1;
}
}  // namespace AMMediaboard
