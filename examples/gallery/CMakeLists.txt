add_executable(silta_gallery WIN32 main.cpp)
target_link_libraries(silta_gallery PRIVATE Silta::app)
if(MSVC)
  target_compile_options(silta_gallery PRIVATE /W4 /utf-8)
  # WIN32 subsystem without a WinMain: use main() as the entry point.
  target_link_options(silta_gallery PRIVATE /ENTRY:mainCRTStartup)
endif()
# Fonts next to the executable, where silta::init looks first.
add_custom_command(TARGET silta_gallery POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${PROJECT_SOURCE_DIR}/assets/fonts $<TARGET_FILE_DIR:silta_gallery>/fonts)
