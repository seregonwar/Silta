# Third-party dependencies, pinned to exact versions.
#
# Dear ImGui: Silta fetches and builds its own copy (target Silta::imgui), or
# uses the host's with -DSILTA_IMGUI_TARGET=<target>; ImGui 1.92.8 or newer.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

set(SILTA_IMGUI_TARGET "" CACHE STRING
    "Existing Dear ImGui target to build against (1.92.8+); empty fetches ImGui 1.92.9b")
set(SILTA_IMGUI_BACKENDS_DIR "" CACHE PATH
    "Folder with ImGui's imgui_impl_*.cpp for silta_app (empty: the target already provides the backends)")
set(SILTA_IMGUI_MIN_VERSION 19280) # IMGUI_VERSION_NUM of 1.92.8

if(SILTA_IMGUI_TARGET)
  # ---------------------------------------------------- the host's ImGui
  if(NOT TARGET ${SILTA_IMGUI_TARGET})
    message(FATAL_ERROR "Silta: SILTA_IMGUI_TARGET '${SILTA_IMGUI_TARGET}' is not a target. "
                        "Create or import it before adding Silta.")
  endif()
  get_target_property(_silta_imgui_dirs ${SILTA_IMGUI_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
  string(REGEX REPLACE "\\$<BUILD_INTERFACE:([^>]*)>" "\\1" _silta_imgui_dirs "${_silta_imgui_dirs}")
  string(GENEX_STRIP "${_silta_imgui_dirs}" _silta_imgui_dirs)
  set(_silta_imgui_header "")
  foreach(_dir IN LISTS _silta_imgui_dirs)
    if(EXISTS "${_dir}/imgui.h")
      set(_silta_imgui_header "${_dir}/imgui.h")
      break()
    endif()
  endforeach()
  if(_silta_imgui_header)
    file(STRINGS "${_silta_imgui_header}" _silta_imgui_version REGEX "#define IMGUI_VERSION_NUM")
    string(REGEX MATCH "[0-9]+" _silta_imgui_version "${_silta_imgui_version}")
    if(_silta_imgui_version LESS SILTA_IMGUI_MIN_VERSION)
      message(FATAL_ERROR "Silta needs Dear ImGui 1.92.8 or newer; ${SILTA_IMGUI_TARGET} is "
                          "${_silta_imgui_version} (IMGUI_VERSION_NUM).")
    endif()
    message(STATUS "Silta: using ImGui target ${SILTA_IMGUI_TARGET} (IMGUI_VERSION_NUM ${_silta_imgui_version})")
    get_filename_component(_silta_imgui_root "${_silta_imgui_header}" DIRECTORY)
    if(NOT SILTA_IMGUI_BACKENDS_DIR AND EXISTS "${_silta_imgui_root}/backends/imgui_impl_glfw.cpp")
      set(SILTA_IMGUI_BACKENDS_DIR "${_silta_imgui_root}/backends")
    endif()
  else()
    message(WARNING "Silta: imgui.h not found in ${SILTA_IMGUI_TARGET}'s include directories; "
                    "the ImGui version (1.92.8+ required) cannot be checked.")
  endif()
  set(SILTA_IMGUI_LINK ${SILTA_IMGUI_TARGET})
  set(SILTA_VENDORED_IMGUI OFF)
  set(SILTA_VENDORED_FREETYPE OFF) # FreeType, if any, is the host's choice
else()
  # ------------------------------------------------------- Silta's ImGui
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(imgui)

  if(SILTA_FREETYPE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype
      GIT_REPOSITORY https://github.com/freetype/freetype.git
      GIT_TAG VER-2-13-3
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(freetype)
  endif()

  # Dear ImGui is Silta's infrastructure layer: draw lists, IO, IDs, windows
  # and the renderer backends. Silta's widgets do not use ImGui's own widgets.
  add_library(silta_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp)
  add_library(Silta::imgui ALIAS silta_imgui)
  set_target_properties(silta_imgui PROPERTIES EXPORT_NAME imgui POSITION_INDEPENDENT_CODE ON)
  target_include_directories(silta_imgui PUBLIC
    $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
    $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>
    $<INSTALL_INTERFACE:include/silta/imgui>)
  # 32-bit characters: emoji and other symbols live beyond U+FFFF.
  target_compile_definitions(silta_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_USE_WCHAR32)
  if(SILTA_FREETYPE)
    target_sources(silta_imgui PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
    target_compile_definitions(silta_imgui PUBLIC IMGUI_ENABLE_FREETYPE)
    target_link_libraries(silta_imgui PUBLIC freetype)
  endif()
  set(SILTA_IMGUI_LINK silta_imgui)
  set(SILTA_IMGUI_BACKENDS_DIR "${imgui_SOURCE_DIR}/backends")
  set(SILTA_VENDORED_IMGUI ON)
  set(SILTA_VENDORED_FREETYPE ${SILTA_FREETYPE})
endif()

# --------------------------------------------------------------- shaping
# Complex scripts (Arabic, Hebrew, Indic, Thai, ...) are shaped by HarfBuzz,
# ordered by SheenBidi (Unicode bidirectional algorithm) and rasterized by
# FreeType into Silta's own glyph atlas. Both are built from source as static
# libraries; licences: HarfBuzz "Old MIT", SheenBidi Apache-2.0.
set(SILTA_SHAPING_ACTIVE OFF)
if(SILTA_SHAPING)
  if(NOT TARGET freetype)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype
      GIT_REPOSITORY https://github.com/freetype/freetype.git
      GIT_TAG VER-2-13-3
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(freetype)
  endif()
  # Sources only (their own CMake projects add tests and install rules).
  FetchContent_Declare(harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG 14.6.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR silta-sources-only)
  FetchContent_Declare(sheenbidi
    GIT_REPOSITORY https://github.com/Tehreer/SheenBidi.git
    GIT_TAG v2.9.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR silta-sources-only)
  FetchContent_MakeAvailable(harfbuzz sheenbidi)

  # HarfBuzz's single-file amalgamation; no FreeType/ICU/GLib integration needed.
  add_library(silta_harfbuzz STATIC ${harfbuzz_SOURCE_DIR}/src/harfbuzz.cc)
  set_target_properties(silta_harfbuzz PROPERTIES EXPORT_NAME harfbuzz POSITION_INDEPENDENT_CODE ON)
  target_include_directories(silta_harfbuzz PUBLIC $<BUILD_INTERFACE:${harfbuzz_SOURCE_DIR}/src>)
  target_compile_definitions(silta_harfbuzz PRIVATE HB_NO_MT)
  if(MSVC)
    target_compile_options(silta_harfbuzz PRIVATE /bigobj /W0)
  endif()

  add_library(silta_sheenbidi STATIC ${sheenbidi_SOURCE_DIR}/Source/SheenBidi.c)
  set_target_properties(silta_sheenbidi PROPERTIES EXPORT_NAME sheenbidi POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(silta_sheenbidi PRIVATE SB_CONFIG_UNITY)
  target_include_directories(silta_sheenbidi
    PUBLIC $<BUILD_INTERFACE:${sheenbidi_SOURCE_DIR}/Headers>
    PRIVATE ${sheenbidi_SOURCE_DIR}/Source)
  set(SILTA_SHAPING_ACTIVE ON)
endif()

if(SILTA_APP)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  # Installed alongside Silta so the exported silta_app can find it.
  set(GLFW_INSTALL ${SILTA_INSTALL} CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw)
endif()
