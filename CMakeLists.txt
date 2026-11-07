cmake_minimum_required(VERSION 3.24)
project(Silta VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SILTA_FREETYPE "Rasterize text with FreeType (recommended)" ON)
option(SILTA_APP "Build the application shell (window + renderer backends)" ON)
option(SILTA_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(SILTA_RENDERER_D3D11 "Direct3D 11 renderer backend" ${WIN32})
option(SILTA_RENDERER_VULKAN "Vulkan renderer backend" ON)

include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# silta: theme, layout, animation, drawing and components. Renderer agnostic:
# everything is drawn through ImDrawList, so it works on every backend.
# ---------------------------------------------------------------------------
add_library(silta
  src/color.cpp
  src/theme.cpp
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
target_include_directories(silta PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(silta PUBLIC silta_imgui)
target_compile_definitions(silta PRIVATE
  SILTA_DEFAULT_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
target_compile_definitions(silta PUBLIC
  SILTA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  SILTA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  SILTA_VERSION_PATCH=${PROJECT_VERSION_PATCH})
if(MSVC)
  target_compile_options(silta PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(silta PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# silta_app: optional window + render loop with interchangeable renderers.
# Apps that already own a renderer (like an engine or a video player) skip it
# and drive silta from their own ImGui frame.
# ---------------------------------------------------------------------------
if(SILTA_APP)
  add_library(silta_app src/app/app.cpp src/app/dialogs.cpp)
  add_library(Silta::app ALIAS silta_app)
  target_link_libraries(silta_app PUBLIC silta PRIVATE glfw)
  target_sources(silta_app PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
  target_include_directories(silta_app PRIVATE src)
  if(MSVC)
    target_compile_options(silta_app PRIVATE /W3 /utf-8)
    target_compile_definitions(silta_app PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  if(WIN32)
    target_link_libraries(silta_app PRIVATE ole32 shell32 uuid)
  endif()

  if(SILTA_RENDERER_D3D11)
    target_sources(silta_app PRIVATE
      src/app/renderer_d3d11.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
    target_compile_definitions(silta_app PUBLIC SILTA_HAS_D3D11=1)
    target_link_libraries(silta_app PRIVATE d3d11 dxgi d3dcompiler)
  endif()

  if(SILTA_RENDERER_VULKAN)
    find_package(Vulkan)
    if(Vulkan_FOUND)
      target_sources(silta_app PRIVATE
        src/app/renderer_vulkan.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
      target_compile_definitions(silta_app PUBLIC SILTA_HAS_VULKAN=1)
      target_link_libraries(silta_app PRIVATE Vulkan::Vulkan)
    else()
      message(WARNING "Silta: Vulkan SDK not found, Vulkan backend disabled")
    endif()
  endif()
endif()

if(SILTA_EXAMPLES AND SILTA_APP)
  add_subdirectory(examples/gallery)
  add_subdirectory(examples/hello)
endif()
