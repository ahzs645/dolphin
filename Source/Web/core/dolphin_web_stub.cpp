// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

extern "C"
{
int dolphin_web_core_version()
{
  return 1;
}

int dolphin_web_boot_target()
{
  // 1 is the harness-local target id for the Wii Menu proof path.
  return 1;
}

int boot_wii_menu()
{
  // This is the first stable browser/WASM contract. The implementation will be
  // replaced by a Dolphin Core entry point once the Emscripten target links.
  return 1;
}
}

