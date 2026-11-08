# A host project with its own Dear ImGui target, adding Silta as a
# subdirectory built against that target (SILTA_IMGUI_TARGET).
cmake_minimum_required(VERSION 3.24)
project(silta_byo_imgui LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The host's ImGui (here: the same sources, built by the host as `host_imgui`).
add_library(host_imgui STATIC
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
target_include_directories(host_imgui PUBLIC ${IMGUI_DIR})

set(SILTA_IMGUI_TARGET host_imgui CACHE STRING "" FORCE)
set(SILTA_APP OFF CACHE BOOL "" FORCE)
set(SILTA_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SILTA_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${SILTA_SOURCE} silta)

add_executable(host main.cpp)
target_link_libraries(host PRIVATE Silta::silta host_imgui)
