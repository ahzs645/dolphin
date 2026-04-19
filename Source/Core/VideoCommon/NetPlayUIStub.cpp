// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/NetPlayChatUI.h"
#include "VideoCommon/NetPlayGolfUI.h"

#include <utility>

std::unique_ptr<NetPlayChatUI> g_netplay_chat_ui;
std::unique_ptr<NetPlayGolfUI> g_netplay_golf_ui;

NetPlayChatUI::NetPlayChatUI(std::function<void(const std::string&)> callback)
    : m_message_callback(std::move(callback))
{
}

NetPlayChatUI::~NetPlayChatUI() = default;

void NetPlayChatUI::Display()
{
}

void NetPlayChatUI::AppendChat(std::string message, Color color)
{
}

void NetPlayChatUI::SendChatMessage()
{
}

void NetPlayChatUI::Activate()
{
}

NetPlayGolfUI::NetPlayGolfUI(std::weak_ptr<NetPlay::NetPlayClient> netplay_client)
    : m_netplay_client(std::move(netplay_client))
{
}

NetPlayGolfUI::~NetPlayGolfUI() = default;

void NetPlayGolfUI::Display()
{
}
