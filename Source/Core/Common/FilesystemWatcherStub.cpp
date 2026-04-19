// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

namespace wtr
{
inline namespace watcher
{
class watch
{
};
}  // namespace watcher
}  // namespace wtr

#include "Common/FilesystemWatcher.h"

namespace Common
{
FilesystemWatcher::FilesystemWatcher() = default;
FilesystemWatcher::~FilesystemWatcher() = default;

void FilesystemWatcher::Watch(const std::string& path)
{
}

void FilesystemWatcher::Unwatch(const std::string& path)
{
}
}  // namespace Common
