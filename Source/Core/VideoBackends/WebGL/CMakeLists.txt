add_library(videowebgl
  WebGLContext.cpp
  WebGLContext.h
  WebGLGfx.cpp
  WebGLGfx.h
  WebGLPipeline.cpp
  WebGLPipeline.h
  WebGLShader.cpp
  WebGLShader.h
  WebGLTexture.cpp
  WebGLTexture.h
  WebGLVertexManager.cpp
  WebGLVertexManager.h
  VideoBackend.cpp
  VideoBackend.h
)

target_link_libraries(videowebgl
PUBLIC
  common
  videonull
  videosoftware
  videocommon
)

if(MSVC)
  # Add precompiled header
  target_link_libraries(videowebgl PRIVATE use_pch)
endif()
