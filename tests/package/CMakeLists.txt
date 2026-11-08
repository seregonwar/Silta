# A separate project that consumes an installed Silta, as a user would.
cmake_minimum_required(VERSION 3.24)
project(silta_package_test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Silta 0.2 CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Silta::silta)
if(TARGET Silta::app)
  # The application shell must link too (window, renderers, dialogs).
  add_executable(consumer_app app.cpp)
  target_link_libraries(consumer_app PRIVATE Silta::app)
endif()
