// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/NetPlayClient.h"

#include <string>

#include "Core/HW/EXI/EXI_DeviceIPL.h"
#include "Core/HW/SI/SI_DeviceGCController.h"

namespace NetPlay
{
std::string GetPlayerMappingString(PlayerId pid, const PadMappingArray& pad_map,
                                   const GBAConfigArray& gba_config,
                                   const PadMappingArray& wiimote_map)
{
  return "None";
}

bool IsNetPlayRunning()
{
  return false;
}

void SetSIPollBatching(bool state)
{
}

void SendPowerButtonEvent()
{
}

std::string GetGBASavePath(int pad_num)
{
  return {};
}

PadDetails GetPadDetails(int pad_num)
{
  PadDetails details;
  details.local_pad = pad_num;
  return details;
}

int NumLocalWiimotes()
{
  return 0;
}

void NetPlay_Enable(NetPlayClient* const np)
{
}

void NetPlay_Disable()
{
}

bool NetPlay_GetWiimoteData(const std::span<NetPlayClient::WiimoteDataBatchEntry>& entries)
{
  return false;
}

unsigned int NetPlay_GetLocalWiimoteForSlot(unsigned int slot)
{
  return slot;
}
}  // namespace NetPlay

bool SerialInterface::CSIDevice_GCController::NetPlay_GetInput(int pad_num, GCPadStatus* status)
{
  return false;
}

int SerialInterface::CSIDevice_GCController::NetPlay_InGamePadToLocalPad(int pad_num)
{
  return pad_num;
}

u64 ExpansionInterface::CEXIIPL::NetPlay_GetEmulatedTime()
{
  return 0;
}
