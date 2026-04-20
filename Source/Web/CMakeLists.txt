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
  "SHELL:-s MIN_WEBGL_VERSION=2"
  "SHELL:-s MAX_WEBGL_VERSION=2"
  "SHELL:-s FULL_ES3=1"
  "SHELL:-s OFFSCREEN_FRAMEBUFFER=1"
  "SHELL:-s OFFSCREENCANVAS_SUPPORT=1"
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:-s INITIAL_MEMORY=384MB"
  "SHELL:-s MAXIMUM_MEMORY=2GB"
  "SHELL:-s NO_EXIT_RUNTIME=1"
  "SHELL:-s EXPORTED_FUNCTIONS=['_boot_wii_menu','_dolphin_web_boot_path','_dolphin_web_boot_target','_dolphin_web_capture_efb','_dolphin_web_core_version','_dolphin_web_efb_buffer','_dolphin_web_efb_height','_dolphin_web_efb_stride','_dolphin_web_efb_version','_dolphin_web_efb_width','_dolphin_web_frame_buffer','_dolphin_web_frame_copy_count','_dolphin_web_frame_copy_megabytes','_dolphin_web_frame_copy_milliseconds','_dolphin_web_frame_height','_dolphin_web_frame_stride','_dolphin_web_frame_version','_dolphin_web_frame_width','_dolphin_web_has_frame','_dolphin_web_initialize','_dolphin_web_initialize_with_options','_dolphin_web_initialize_with_renderer','_dolphin_web_last_status','_dolphin_web_perf_cpu_milliseconds','_dolphin_web_perf_cpu_samples','_dolphin_web_perf_fps','_dolphin_web_perf_max_speed','_dolphin_web_perf_native_draws','_dolphin_web_perf_native_framebuffer_copies','_dolphin_web_perf_native_gl_errors','_dolphin_web_perf_native_readbacks','_dolphin_web_perf_native_texture_loads','_dolphin_web_perf_native_vertices_or_indices','_dolphin_web_perf_presented_frames','_dolphin_web_perf_presented_height','_dolphin_web_perf_presented_width','_dolphin_web_perf_speed','_dolphin_web_perf_sw_raster_batches','_dolphin_web_perf_sw_raster_milliseconds','_dolphin_web_perf_sw_raster_vertices','_dolphin_web_perf_vps','_dolphin_web_perf_webgl_present_milliseconds','_dolphin_web_perf_webgl_presents','_dolphin_web_perf_webgl_upload_megabytes','_dolphin_web_perf_webgl_upload_milliseconds','_dolphin_web_perf_webgl_uploads','_dolphin_web_perf_xfb_copies','_dolphin_web_perf_xfb_copy_megabytes','_dolphin_web_perf_xfb_copy_milliseconds','_dolphin_web_pump_host_jobs','_dolphin_web_set_wiimote_mouse','_dolphin_web_state','_dolphin_web_stop','_dolphin_web_wiimote_mouse_active','_dolphin_web_wiimote_mouse_buttons','_dolphin_web_wiimote_mouse_x','_dolphin_web_wiimote_mouse_y']"
  "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','HEAPU8']"
)
