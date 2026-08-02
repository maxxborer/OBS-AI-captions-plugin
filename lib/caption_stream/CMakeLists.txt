project(caption_stream)

include_directories(".")

find_package(SherpaOnnx REQUIRED)

add_library(
  caption_stream STATIC
  CaptionEngine.h
  CaptionResult.h
  LocalCaptionEngineSettings.h
  SherpaTOneCaptionEngine.cpp
  SherpaTOneCaptionEngine.h
  ThreadSafeCallback.h
)

target_include_directories(caption_stream PUBLIC ./)
target_link_libraries(caption_stream PUBLIC SherpaOnnx::CAPI)

if(UNIX AND NOT APPLE)
  target_compile_options(caption_stream PRIVATE "-fPIC")
endif()
