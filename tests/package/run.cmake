# ctest driver: install the current build into a private prefix, then
# configure, build and run tests/package against it.
#   -DSILTA_BUILD_DIR -DCONSUMER_SOURCE -DWORK_DIR -DCONFIG -DGENERATOR [-DPLATFORM]
set(prefix "${WORK_DIR}/prefix")
set(build "${WORK_DIR}/build")
file(REMOVE_RECURSE "${WORK_DIR}")

function(run)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Command failed (${result}): ${ARGV}\n${out}\n${err}")
  endif()
endfunction()

if(NOT CONFIG)
  set(CONFIG Release)
endif()
run(${CMAKE_COMMAND} --install "${SILTA_BUILD_DIR}" --prefix "${prefix}" --config ${CONFIG})

set(platform_args "")
if(PLATFORM)
  set(platform_args -A ${PLATFORM})
endif()
# Same MSVC runtime as the Silta build (static or DLL), or the link fails.
if(MSVC_RUNTIME)
  list(APPEND platform_args "-DCMAKE_MSVC_RUNTIME_LIBRARY=${MSVC_RUNTIME}")
endif()
run(${CMAKE_COMMAND} -S "${CONSUMER_SOURCE}" -B "${build}" -G "${GENERATOR}" ${platform_args}
    "-DCMAKE_PREFIX_PATH=${prefix}" "-DCMAKE_BUILD_TYPE=${CONFIG}")
run(${CMAKE_COMMAND} --build "${build}" --config ${CONFIG})

# Single- and multi-config generators put the executable in different places.
foreach(candidate "${build}/${CONFIG}/consumer${CMAKE_EXECUTABLE_SUFFIX}" "${build}/consumer${CMAKE_EXECUTABLE_SUFFIX}"
                  "${build}/${CONFIG}/consumer.exe" "${build}/consumer.exe" "${build}/consumer")
  if(EXISTS "${candidate}")
    set(exe "${candidate}")
    break()
  endif()
endforeach()
if(NOT exe)
  message(FATAL_ERROR "consumer executable not found in ${build}")
endif()
run("${exe}")
message(STATUS "Silta package test passed")
