cmake_minimum_required(VERSION 3.24)
project(Silta VERSION 0.2.0 LANGUAGES C CXX)
include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SILTA_FREETYPE "Rasterize text with FreeType (recommended; with the fetched ImGui)" ON)
option(SILTA_EMBED_FONTS "Build the Inter fonts into the library" ON)
option(SILTA_APP "Build the application shell (window + renderer backends)" ON)
option(SILTA_INSTALL "Generate install rules and the Silta CMake package" ${PROJECT_IS_TOP_LEVEL})
option(SILTA_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(SILTA_EDITOR "Build Silta Cut (requires third_party/ffmpeg)" OFF)
option(SILTA_RENDERER_D3D11 "Direct3D 11 renderer backend" ${WIN32})
option(SILTA_RENDERER_VULKAN "Vulkan renderer backend" ON)

set(SILTA_APP_VULKAN OFF) # set when the Vulkan renderer is built

include(cmake/Dependencies.cmake)
include(cmake/EmbedFonts.cmake)

# ---------------------------------------------------------------------------
# silta: theme, layout, animation, drawing and components. Renderer agnostic:
# everything is drawn through ImDrawList, so it works on every backend.
# ---------------------------------------------------------------------------
add_library(silta
  src/color.cpp
  src/theme.cpp
  src/per_context.cpp
  src/fonts.cpp
  src/context.cpp
  src/anim.cpp
  src/layout.cpp
  src/draw.cpp
  src/icons.cpp
  src/text.cpp
  src/texture.cpp
  src/components/button.cpp
  src/components/badge.cpp
  src/components/card.cpp
  src/components/switch.cpp
  src/components/input.cpp
  src/components/separator.cpp
  src/components/overlay.cpp
  src/components/controls.cpp
  src/components/panels.cpp
  src/components/toast.cpp
)
add_library(Silta::silta ALIAS silta)
set_target_properties(silta PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(silta PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(silta PUBLIC ${SILTA_IMGUI_LINK})
target_compile_definitions(silta PUBLIC
  SILTA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  SILTA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  SILTA_VERSION_PATCH=${PROJECT_VERSION_PATCH})
if(SILTA_EMBED_FONTS)
  set(_silta_fonts_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/silta_fonts.cpp")
  silta_embed_fonts("${_silta_fonts_cpp}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
  target_sources(silta PRIVATE "${_silta_fonts_cpp}")
  target_compile_definitions(silta PRIVATE SILTA_EMBED_FONTS)
else()
  # Development fallback only: release builds should embed or ship the fonts.
  target_compile_definitions(silta PRIVATE
    SILTA_DEFAULT_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
endif()
if(MSVC)
  target_compile_options(silta PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(silta PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# silta_app: optional window + render loop with interchangeable renderers.
# Apps that already own a renderer (like an engine or a video player) skip it
# and drive silta from their own ImGui frame (see docs/embedding.md).
# ---------------------------------------------------------------------------
if(SILTA_APP)
  add_library(silta_app src/app/app.cpp src/app/dialogs.cpp)
  add_library(Silta::app ALIAS silta_app)
  set_target_properties(silta_app PROPERTIES EXPORT_NAME app POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(silta_app PUBLIC silta PRIVATE glfw)
  target_include_directories(silta_app PRIVATE src)
  if(SILTA_IMGUI_BACKENDS_DIR)
    target_sources(silta_app PRIVATE ${SILTA_IMGUI_BACKENDS_DIR}/imgui_impl_glfw.cpp)
    target_include_directories(silta_app PRIVATE ${SILTA_IMGUI_BACKENDS_DIR})
  endif()
  if(MSVC)
    target_compile_options(silta_app PRIVATE /W3 /utf-8)
    target_compile_definitions(silta_app PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  if(WIN32)
    target_link_libraries(silta_app PRIVATE ole32 shell32 uuid)
  endif()

  if(SILTA_RENDERER_D3D11)
    target_sources(silta_app PRIVATE src/app/renderer_d3d11.cpp)
    if(SILTA_IMGUI_BACKENDS_DIR)
      target_sources(silta_app PRIVATE ${SILTA_IMGUI_BACKENDS_DIR}/imgui_impl_dx11.cpp)
    endif()
    target_compile_definitions(silta_app PUBLIC SILTA_HAS_D3D11=1)
    target_link_libraries(silta_app PRIVATE d3d11 dxgi d3dcompiler)
  endif()

  if(SILTA_RENDERER_VULKAN)
    find_package(Vulkan)
    if(Vulkan_FOUND)
      target_sources(silta_app PRIVATE src/app/renderer_vulkan.cpp)
      if(SILTA_IMGUI_BACKENDS_DIR)
        target_sources(silta_app PRIVATE ${SILTA_IMGUI_BACKENDS_DIR}/imgui_impl_vulkan.cpp)
      endif()
      target_compile_definitions(silta_app PUBLIC SILTA_HAS_VULKAN=1)
      target_link_libraries(silta_app PRIVATE Vulkan::Vulkan)
      set(SILTA_APP_VULKAN ON)
    else()
      message(WARNING "Silta: Vulkan SDK not found, Vulkan backend disabled")
    endif()
  endif()
endif()

if(SILTA_EXAMPLES AND SILTA_APP)
  add_subdirectory(examples/gallery)
  add_subdirectory(examples/hello)
endif()
if(SILTA_EXAMPLES AND WIN32 AND SILTA_VENDORED_IMGUI)
  add_subdirectory(examples/embed)
endif()

if(SILTA_EDITOR AND SILTA_APP)
  add_subdirectory(examples/editor)
endif()

# ---------------------------------------------------------------------------
# Install and package: find_package(Silta CONFIG) gives Silta::silta and,
# when built, Silta::app.
# ---------------------------------------------------------------------------
if(SILTA_INSTALL AND SILTA_IMGUI_TARGET)
  get_target_property(_silta_imgui_imported ${SILTA_IMGUI_TARGET} IMPORTED)
  if(NOT _silta_imgui_imported)
    message(WARNING "Silta: install rules disabled: ${SILTA_IMGUI_TARGET} is built in this project, so "
                    "the package could not refer to it. Use an imported ImGui target (e.g. from a "
                    "package manager) to install Silta.")
    set(SILTA_INSTALL OFF)
  endif()
endif()

if(SILTA_INSTALL)
  include(CMakePackageConfigHelpers)
  set(_silta_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/Silta)
  set(_silta_targets silta)
  if(SILTA_VENDORED_IMGUI)
    list(APPEND _silta_targets silta_imgui)
  endif()
  if(SILTA_APP)
    list(APPEND _silta_targets silta_app)
  endif()
  install(TARGETS ${_silta_targets} EXPORT SiltaTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY include/silta DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  if(SILTA_VENDORED_IMGUI)
    # ImGui's headers live under include/silta/imgui so they cannot clash with
    # another ImGui installed in the same prefix.
    install(FILES
      ${imgui_SOURCE_DIR}/imgui.h ${imgui_SOURCE_DIR}/imconfig.h ${imgui_SOURCE_DIR}/imgui_internal.h
      ${imgui_SOURCE_DIR}/imstb_textedit.h ${imgui_SOURCE_DIR}/imstb_rectpack.h ${imgui_SOURCE_DIR}/imstb_truetype.h
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/silta/imgui)
    install(DIRECTORY ${imgui_SOURCE_DIR}/backends/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/silta/imgui
      FILES_MATCHING PATTERN "imgui_impl_*.h" PATTERN "vulkan" EXCLUDE PATTERN "sdlgpu3" EXCLUDE)
    install(FILES ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.h
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/silta/imgui/misc/freetype)
  endif()
  install(EXPORT SiltaTargets NAMESPACE Silta:: DESTINATION ${_silta_cmake_dir})
  set(SILTA_MSVC_RUNTIME "${CMAKE_MSVC_RUNTIME_LIBRARY}")
  if(NOT SILTA_MSVC_RUNTIME)
    set(SILTA_MSVC_RUNTIME "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL") # CMake's default
  endif()
  configure_package_config_file(cmake/SiltaConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SiltaConfig.cmake INSTALL_DESTINATION ${_silta_cmake_dir})
  # Before 1.0 a minor release may break the API: only the same minor matches.
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/SiltaConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SiltaConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/SiltaConfigVersion.cmake
    DESTINATION ${_silta_cmake_dir})
  install(FILES LICENSE THIRD_PARTY.md assets/fonts/Inter-LICENSE.txt DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  add_executable(silta_core_test tests/core.cpp)
  target_link_libraries(silta_core_test PRIVATE Silta::silta)
  add_test(NAME silta.core COMMAND silta_core_test)
  set_tests_properties(silta.core PROPERTIES TIMEOUT 30)
  if(WIN32 AND SILTA_APP AND SILTA_RENDERER_D3D11)
    add_executable(silta_app_test tests/app.cpp)
    target_link_libraries(silta_app_test PRIVATE Silta::app)
    add_test(NAME silta.app.hidden-size COMMAND silta_app_test)
    set_tests_properties(silta.app.hidden-size PROPERTIES TIMEOUT 30)
  endif()
  if(SILTA_INSTALL)
    # Installs Silta into the build tree and builds a separate project that
    # finds it with find_package(Silta).
    add_test(NAME silta.package
      COMMAND ${CMAKE_COMMAND}
        -DSILTA_BUILD_DIR=${CMAKE_BINARY_DIR}
        -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/package
        -DWORK_DIR=${CMAKE_BINARY_DIR}/package-test
        -DCONFIG=$<CONFIG>
        -DGENERATOR=${CMAKE_GENERATOR}
        -DPLATFORM=${CMAKE_GENERATOR_PLATFORM}
        "-DMSVC_RUNTIME=${CMAKE_MSVC_RUNTIME_LIBRARY}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/package/run.cmake)
    set_tests_properties(silta.package PROPERTIES TIMEOUT 600)
  endif()
  if(SILTA_VENDORED_IMGUI)
    # A host project with its own ImGui target builds Silta against it.
    add_test(NAME silta.byo-imgui
      COMMAND ${CMAKE_COMMAND}
        -DSILTA_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}
        -DIMGUI_DIR=${imgui_SOURCE_DIR}
        -DWORK_DIR=${CMAKE_BINARY_DIR}/byo-imgui-test
        -DCONFIG=$<CONFIG>
        -DGENERATOR=${CMAKE_GENERATOR}
        -DPLATFORM=${CMAKE_GENERATOR_PLATFORM}
        "-DMSVC_RUNTIME=${CMAKE_MSVC_RUNTIME_LIBRARY}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/byo_imgui/run.cmake)
    set_tests_properties(silta.byo-imgui PROPERTIES TIMEOUT 600)
  endif()
endif()
