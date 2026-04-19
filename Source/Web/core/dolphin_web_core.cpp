// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <emscripten/emscripten.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>

#include "Common/Config/Config.h"
#include "Common/WindowSystemInfo.h"

#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/CommonTitles.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"

#include "UICommon/UICommon.h"

#include "InputCommon/ControllerInterface/Touch/InputOverrider.h"

#include "VideoCommon/PerformanceMetrics.h"
#include "VideoCommon/VideoBackendBase.h"

namespace
{
constexpr int BOOT_TARGET_WII_MENU = 1;
constexpr int MAX_WEB_WIIMOTES = 4;
constexpr int MOUSE_COORDINATE_SCALE = 10000;

struct WebWiimoteMouseState
{
  int x = MOUSE_COORDINATE_SCALE / 2;
  int y = MOUSE_COORDINATE_SCALE / 2;
  int buttons = 0;
  bool active = false;
};

bool s_ui_initialized = false;
bool s_web_wii_input_registered = false;
std::string s_last_status = "Dolphin web core is not initialized.";
std::array<WebWiimoteMouseState, MAX_WEB_WIIMOTES> s_web_wiimote_mouse_state;

WindowSystemInfo MakeWebWindowSystemInfo()
{
  WindowSystemInfo wsi;
  wsi.type = WindowSystemType::Web;
  return wsi;
}

void SetStatus(std::string status)
{
  s_last_status = std::move(status);
}

int GetCoreState()
{
  return static_cast<int>(Core::GetState(Core::System::GetInstance()));
}

bool IsValidWiimoteIndex(int wiimote_index)
{
  return wiimote_index >= 0 && wiimote_index < MAX_WEB_WIIMOTES;
}

void ApplyWebWiimoteMouseState(int wiimote_index)
{
  if (!s_web_wii_input_registered || !IsValidWiimoteIndex(wiimote_index))
    return;

  const WebWiimoteMouseState& state = s_web_wiimote_mouse_state[wiimote_index];

  // The touch input overrider uses the same -1..1 IR X/Y space as Dolphin's Android overlay.
  const double x = static_cast<double>(state.x) / (MOUSE_COORDINATE_SCALE / 2.0) - 1.0;
  const double y = 1.0 - static_cast<double>(state.y) / (MOUSE_COORDINATE_SCALE / 2.0);

  if (state.active)
  {
    ciface::Touch::SetControlState(wiimote_index, ciface::Touch::WIIMOTE_IR_X, x);
    ciface::Touch::SetControlState(wiimote_index, ciface::Touch::WIIMOTE_IR_Y, y);
  }
  else
  {
    ciface::Touch::SetControlState(wiimote_index, ciface::Touch::WIIMOTE_IR_X,
                                   std::numeric_limits<double>::quiet_NaN());
    ciface::Touch::SetControlState(wiimote_index, ciface::Touch::WIIMOTE_IR_Y, 0.0);
  }

  ciface::Touch::SetControlState(wiimote_index, ciface::Touch::WIIMOTE_A_BUTTON,
                                 state.active && (state.buttons & 1) ? 1.0 : 0.0);
  ciface::Touch::SetControlState(wiimote_index, ciface::Touch::WIIMOTE_B_BUTTON,
                                 state.active && (state.buttons & 2) ? 1.0 : 0.0);
  ciface::Touch::SetControlState(wiimote_index, ciface::Touch::WIIMOTE_HOME_BUTTON,
                                 state.active && (state.buttons & 4) ? 1.0 : 0.0);
}

void RegisterWebWiiInput()
{
  if (s_web_wii_input_registered)
    return;

  ciface::Touch::RegisterWiiInputOverrider(0);
  s_web_wii_input_registered = true;
  ApplyWebWiimoteMouseState(0);
}

void UnregisterWebWiiInput()
{
  if (!s_web_wii_input_registered)
    return;

  ciface::Touch::UnregisterWiiInputOverrider(0);
  s_web_wii_input_registered = false;
}

void ApplyWebPerformanceConfig()
{
  Config::SetCurrent(Config::MAIN_CPU_CORE, PowerPC::CPUCore::CachedInterpreter);
  Config::SetCurrent(Config::MAIN_CPU_THREAD, true);
  Config::SetCurrent(Config::MAIN_DSP_HLE, true);
  Config::SetCurrent(Config::MAIN_DSP_THREAD, true);
}
}  // namespace

extern "C"
{
EMSCRIPTEN_KEEPALIVE int dolphin_web_core_version()
{
  return 2;
}

EMSCRIPTEN_KEEPALIVE int dolphin_web_boot_target()
{
  return BOOT_TARGET_WII_MENU;
}

EMSCRIPTEN_KEEPALIVE const char* dolphin_web_last_status()
{
  return s_last_status.c_str();
}

EMSCRIPTEN_KEEPALIVE int dolphin_web_state()
{
  return GetCoreState();
}

EMSCRIPTEN_KEEPALIVE int dolphin_web_initialize()
{
  if (s_ui_initialized)
  {
    SetStatus("Dolphin UICommon is already initialized.");
    return 1;
  }

  const WindowSystemInfo wsi = MakeWebWindowSystemInfo();

  UICommon::SetUserDirectory("/dolphin-user");
  UICommon::Init();

  ApplyWebPerformanceConfig();
  Config::SetCurrent(Config::MAIN_GFX_BACKEND, std::string("Software Renderer"));
  VideoBackendBase::ActivateBackend("Software Renderer");

  UICommon::InitControllers(wsi);
  RegisterWebWiiInput();

  s_ui_initialized = true;
  SetStatus("Dolphin UICommon initialized with the browser software renderer and mouse-backed "
            "Wii Remote input.");
  return 1;
}

EMSCRIPTEN_KEEPALIVE int dolphin_web_set_wiimote_mouse(int wiimote_index, int x, int y,
                                                       int buttons, int active)
{
  if (!IsValidWiimoteIndex(wiimote_index))
    return -1;

  WebWiimoteMouseState& state = s_web_wiimote_mouse_state[wiimote_index];
  state.x = std::clamp(x, 0, MOUSE_COORDINATE_SCALE);
  state.y = std::clamp(y, 0, MOUSE_COORDINATE_SCALE);
  state.buttons = buttons & 7;
  state.active = active != 0;

  ApplyWebWiimoteMouseState(wiimote_index);
  return 1;
}

EMSCRIPTEN_KEEPALIVE int dolphin_web_wiimote_mouse_x(int wiimote_index)
{
  return IsValidWiimoteIndex(wiimote_index) ? s_web_wiimote_mouse_state[wiimote_index].x : -1;
}

EMSCRIPTEN_KEEPALIVE int dolphin_web_wiimote_mouse_y(int wiimote_index)
{
  return IsValidWiimoteIndex(wiimote_index) ? s_web_wiimote_mouse_state[wiimote_index].y : -1;
}

EMSCRIPTEN_KEEPALIVE int dolphin_web_wiimote_mouse_buttons(int wiimote_index)
{
  return IsValidWiimoteIndex(wiimote_index) ? s_web_wiimote_mouse_state[wiimote_index].buttons : -1;
}

EMSCRIPTEN_KEEPALIVE int dolphin_web_wiimote_mouse_active(int wiimote_index)
{
  if (!IsValidWiimoteIndex(wiimote_index))
    return 0;

  return s_web_wiimote_mouse_state[wiimote_index].active ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int boot_wii_menu()
{
  if (!s_ui_initialized && dolphin_web_initialize() <= 0)
    return -10;

  Core::System& system = Core::System::GetInstance();
  const Core::State state = Core::GetState(system);
  if (state != Core::State::Uninitialized)
  {
    SetStatus("Dolphin core is already running or stopping.");
    return 2;
  }

  auto boot = std::make_unique<BootParameters>(BootParameters::NANDTitle{Titles::SYSTEM_MENU});
  if (!BootManager::BootCore(system, std::move(boot), MakeWebWindowSystemInfo()))
  {
    SetStatus("BootCore rejected the Wii Menu title. A browser-mounted Wii NAND is required.");
    return -20;
  }

  SetStatus("BootCore accepted the Wii Menu title.");
  return 1;
}

EMSCRIPTEN_KEEPALIVE int dolphin_web_boot_path(const char* path)
{
  if (!path || path[0] == '\0')
  {
    SetStatus("No boot path was provided.");
    return -30;
  }

  if (!s_ui_initialized && dolphin_web_initialize() <= 0)
    return -10;

  Core::System& system = Core::System::GetInstance();
  const Core::State state = Core::GetState(system);
  if (state != Core::State::Uninitialized)
  {
    SetStatus("Dolphin core is already running or stopping.");
    return 2;
  }

  auto boot = BootParameters::GenerateFromFile(path);
  if (!boot)
  {
    SetStatus(std::string("Dolphin could not recognize boot file: ") + path);
    return -31;
  }

  if (!BootManager::BootCore(system, std::move(boot), MakeWebWindowSystemInfo()))
  {
    SetStatus(std::string("BootCore rejected boot file: ") + path);
    return -32;
  }

  SetStatus(std::string("BootCore accepted boot file: ") + path);
  return 1;
}

EMSCRIPTEN_KEEPALIVE int dolphin_web_pump_host_jobs()
{
  if (!s_ui_initialized)
    return -1;

  Core::System& system = Core::System::GetInstance();
  Core::HostDispatchJobs(system);
  return GetCoreState();
}

EMSCRIPTEN_KEEPALIVE double dolphin_web_perf_fps()
{
  return Core::System::GetInstance().GetPerfMetrics().GetFPS();
}

EMSCRIPTEN_KEEPALIVE double dolphin_web_perf_vps()
{
  return Core::System::GetInstance().GetPerfMetrics().GetVPS();
}

EMSCRIPTEN_KEEPALIVE double dolphin_web_perf_speed()
{
  return Core::System::GetInstance().GetPerfMetrics().GetSpeed();
}

EMSCRIPTEN_KEEPALIVE double dolphin_web_perf_max_speed()
{
  return Core::System::GetInstance().GetPerfMetrics().GetMaxSpeed();
}

EMSCRIPTEN_KEEPALIVE void dolphin_web_stop()
{
  if (!s_ui_initialized)
    return;

  Core::System& system = Core::System::GetInstance();
  if (Core::GetState(system) != Core::State::Uninitialized)
    Core::Stop(system);

  Core::Shutdown(system);
  UnregisterWebWiiInput();
  UICommon::ShutdownControllers();
  UICommon::Shutdown();
  s_ui_initialized = false;
  SetStatus("Dolphin web core stopped.");
}
}
