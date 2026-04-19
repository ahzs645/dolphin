if(NOT EMSCRIPTEN)
  return()
endif()

add_executable(dolphin-web-core
  core/dolphin_web_core.cpp
  core/dolphin_web_host.cpp
)

set_target_properties(dolphin-web-core PROPERTIES
  OUTPUT_NAME dolphin-web-core
)

target_link_libraries(dolphin-web-core
PRIVATE
  core
  uicommon
)

target_link_options(dolphin-web-core
PRIVATE
  "SHELL:-s MODULARIZE=1"
  "SHELL:-s EXPORT_ES6=1"
  "SHELL:-s EXPORT_NAME=createDolphinCore"
  "SHELL:-s ENVIRONMENT=web,worker"
  "SHELL:-s USE_PTHREADS=1"
  "SHELL:-s PTHREAD_POOL_SIZE=4"
  "SHELL:-msimd128"
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:-s INITIAL_MEMORY=384MB"
  "SHELL:-s MAXIMUM_MEMORY=2GB"
  "SHELL:-s NO_EXIT_RUNTIME=1"
  "SHELL:-s EXPORTED_FUNCTIONS=['_boot_wii_menu','_dolphin_web_boot_path','_dolphin_web_boot_target','_dolphin_web_capture_efb','_dolphin_web_core_version','_dolphin_web_efb_buffer','_dolphin_web_efb_height','_dolphin_web_efb_stride','_dolphin_web_efb_version','_dolphin_web_efb_width','_dolphin_web_frame_buffer','_dolphin_web_frame_height','_dolphin_web_frame_stride','_dolphin_web_frame_version','_dolphin_web_frame_width','_dolphin_web_has_frame','_dolphin_web_initialize','_dolphin_web_last_status','_dolphin_web_perf_fps','_dolphin_web_perf_max_speed','_dolphin_web_perf_speed','_dolphin_web_perf_vps','_dolphin_web_pump_host_jobs','_dolphin_web_set_wiimote_mouse','_dolphin_web_state','_dolphin_web_stop','_dolphin_web_wiimote_mouse_active','_dolphin_web_wiimote_mouse_buttons','_dolphin_web_wiimote_mouse_x','_dolphin_web_wiimote_mouse_y']"
  "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','HEAPU8']"
)
