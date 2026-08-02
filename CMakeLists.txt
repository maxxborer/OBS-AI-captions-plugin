cmake_minimum_required(VERSION 3.28...4.3)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(compilerconfig)
include(defaults)
include(helpers)
include(FindSherpaOnnx)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

add_subdirectory(lib/caption_stream)

add_library(${CMAKE_PROJECT_NAME} MODULE)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/ui/MainCaptionWidget.cpp
    src/ui/CaptionSettingsWidget.cpp
    src/SourceAudioCaptureSession.cpp
    src/OutputAudioCaptureSession.cpp
    src/SourceCaptioner.cpp
    src/CaptionResultHandler.cpp
    src/ai_caption_plugin.cpp
    src/CaptionPluginManager.cpp
    src/BrowserCaptionServer.cpp
    src/ui/MainCaptionWidget.h
    src/ui/CaptionSettingsWidget.h
    src/SourceAudioCaptureSession.h
    src/OutputAudioCaptureSession.h
    src/SourceCaptioner.h
    src/CaptionResultHandler.h
    src/caption_settings_storage.h
    src/obs_frontend_state.h
    src/caption_file_writer.h
    src/caption_output_writer.h
    src/CaptionPluginManager.h
    src/BrowserCaptionServer.h
    src/BrowserOverlaySettings.h
    src/CaptionFileName.h
    src/TextReplacements.h
    src/resources/obs.qrc
)

target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE BUILD_CAPTIONS=1 VERSION_STRING="${PROJECT_VERSION}"
)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE caption_stream Qt6::Core Qt6::Network Qt6::Widgets OBS::libobs OBS::obs-frontend-api
)

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTORCC ON OUTPUT_NAME ai-caption-plugin
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME})
install(FILES ${SHERPA_ONNX_RUNTIME_FILES} DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit")
install(
  FILES
    installer/Install-AICaptionPlugin.cmd
    installer/Install-AICaptionPlugin.ps1
    installer/local-model.json
  DESTINATION "${CMAKE_PROJECT_NAME}"
)
install(
  FILES
    LICENSE
    NOTICE.md
    THIRD_PARTY_NOTICES.md
    third_party/licenses/Apache-2.0.txt
    third_party/licenses/MIT-onnxruntime.txt
    src/resources/fonts/Geologica-OFL.txt
  DESTINATION "${CMAKE_PROJECT_NAME}/licenses"
)
sherpa_onnx_copy_runtime(${CMAKE_PROJECT_NAME})

include(CTest)
if(BUILD_TESTING)
  if(WIN32)
    add_test(
      NAME installer-contract
      COMMAND
        pwsh.exe -NoLogo -NoProfile -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/installer_contract_test.ps1"
    )
    add_test(
      NAME repository-contract
      COMMAND
        pwsh.exe -NoLogo -NoProfile -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/repository_contract_test.ps1"
    )
  endif()

  add_executable(caption-engine-contract-test tests/caption_engine_contract_test.cpp)
  target_include_directories(caption-engine-contract-test PRIVATE lib/caption_stream)
  target_link_libraries(caption-engine-contract-test PRIVATE caption_stream)
  sherpa_onnx_copy_runtime(caption-engine-contract-test)
  add_test(NAME caption-engine-contract COMMAND caption-engine-contract-test)

  add_executable(english-term-replacements-test tests/english_term_replacements_test.cpp)
  target_include_directories(english-term-replacements-test PRIVATE src)
  target_link_libraries(english-term-replacements-test PRIVATE Qt6::Core)
  add_custom_command(
    TARGET english-term-replacements-test
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Core>
      $<TARGET_FILE_DIR:english-term-replacements-test>
  )
  add_test(NAME english-term-replacements COMMAND english-term-replacements-test)

  add_executable(
    caption-result-handler-test
    tests/caption_result_handler_test.cpp
    src/CaptionResultHandler.cpp
  )
  target_include_directories(caption-result-handler-test PRIVATE src lib/caption_stream)
  target_link_libraries(caption-result-handler-test PRIVATE Qt6::Core)
  add_custom_command(
    TARGET caption-result-handler-test
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Core>
      $<TARGET_FILE_DIR:caption-result-handler-test>
  )
  add_test(NAME caption-result-handler COMMAND caption-result-handler-test)

  add_executable(caption-filename-test tests/caption_filename_test.cpp)
  target_include_directories(caption-filename-test PRIVATE src)
  target_link_libraries(caption-filename-test PRIVATE Qt6::Core)
  add_custom_command(
    TARGET caption-filename-test
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Core>
      $<TARGET_FILE_DIR:caption-filename-test>
  )
  add_test(NAME caption-filename COMMAND caption-filename-test)

  add_executable(caption-settings-storage-test tests/caption_settings_storage_test.cpp)
  target_include_directories(
    caption-settings-storage-test
    PRIVATE src lib/caption_stream
  )
  target_compile_definitions(
    caption-settings-storage-test
    PRIVATE VERSION_STRING="${PROJECT_VERSION}"
  )
  target_link_libraries(caption-settings-storage-test PRIVATE Qt6::Core OBS::libobs)
  add_test(NAME caption-settings-storage COMMAND caption-settings-storage-test)
  if(WIN32)
    add_custom_command(
      TARGET caption-settings-storage-test
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:caption-settings-storage-test>
        $<TARGET_FILE_DIR:caption-settings-storage-test>
      COMMAND_EXPAND_LISTS
    )
    set(caption_test_runtime_modifications)
    foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
      if(EXISTS "${prefix}/bin")
        list(APPEND caption_test_runtime_modifications "PATH=path_list_prepend:${prefix}/bin")
      endif()
    endforeach()
    set_tests_properties(
      caption-settings-storage
      PROPERTIES ENVIRONMENT_MODIFICATION "${caption_test_runtime_modifications}"
    )
  endif()

  add_executable(
    browser-caption-server-test
    tests/browser_caption_server_test.cpp
    src/BrowserCaptionServer.cpp
    src/BrowserCaptionServer.h
    src/resources/obs.qrc
  )
  set_target_properties(browser-caption-server-test PROPERTIES AUTOMOC ON AUTORCC ON)
  target_include_directories(browser-caption-server-test PRIVATE src lib/caption_stream)
  target_link_libraries(browser-caption-server-test PRIVATE Qt6::Core Qt6::Network)
  add_custom_command(
    TARGET browser-caption-server-test
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Core> $<TARGET_FILE:Qt6::Network>
      $<TARGET_FILE_DIR:browser-caption-server-test>
  )
  add_test(NAME browser-caption-server COMMAND browser-caption-server-test)
endif()
