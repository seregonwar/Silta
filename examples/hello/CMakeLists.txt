add_executable(silta_hello WIN32 main.cpp)
target_link_libraries(silta_hello PRIVATE Silta::app)
if(MSVC)
  target_compile_options(silta_hello PRIVATE /W4 /utf-8)
  target_link_options(silta_hello PRIVATE /ENTRY:mainCRTStartup)
endif()
# Without SILTA_EMBED_FONTS, Silta looks for the fonts next to the executable.
if(NOT SILTA_EMBED_FONTS)
  add_custom_command(TARGET silta_hello POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/assets/fonts $<TARGET_FILE_DIR:silta_hello>/fonts)
endif()
