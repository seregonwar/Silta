# Silta embedded in a host that owns its window, renderer and ImGui style.
add_executable(silta_embed WIN32 main.cpp
  ${SILTA_IMGUI_BACKENDS_DIR}/imgui_impl_win32.cpp
  ${SILTA_IMGUI_BACKENDS_DIR}/imgui_impl_dx11.cpp)
target_link_libraries(silta_embed PRIVATE Silta::silta d3d11 dxgi d3dcompiler dwmapi shell32)
target_include_directories(silta_embed PRIVATE ${SILTA_IMGUI_BACKENDS_DIR})
if(MSVC)
  target_compile_options(silta_embed PRIVATE /W3 /utf-8)
  target_compile_definitions(silta_embed PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
