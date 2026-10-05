# Configures and builds the emulator of this tree into BUILD_DIR, for the system tests.
#   cmake -DSOURCE_DIR=<repository> -DBUILD_DIR=<dir> -DGENERATOR=<generator> [-DTA_HLE=ON] -P build_emulator.cmake
#
# The emulator's CMakeLists finds its sources by globbing: it is configured again every time, so that a new file
# is built without anyone asking. What the two steps print goes to <BUILD_DIR>.log, and to the console when
# one of them fails.

set(args -S "${SOURCE_DIR}" -B "${BUILD_DIR}" -G "${GENERATOR}" -DCMAKE_BUILD_TYPE=RelWithDebInfo -Wno-dev)
if(TA_HLE)
  list(APPEND args -DHAS_TA_LLE=OFF)
else()
  list(APPEND args -DHAS_TA_LLE=ON)
endif()

set(log "${BUILD_DIR}.log")

execute_process(COMMAND "${CMAKE_COMMAND}" ${args}
                RESULT_VARIABLE result OUTPUT_FILE "${log}" ERROR_FILE "${log}")
if(NOT result EQUAL 0)
  file(READ "${log}" text)
  message(FATAL_ERROR "${text}\nThe emulator in ${BUILD_DIR} did not configure (${log})")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}"
                RESULT_VARIABLE result OUTPUT_FILE "${log}" ERROR_FILE "${log}")
if(NOT result EQUAL 0)
  file(READ "${log}" text)
  string(LENGTH "${text}" length)
  if(length GREATER 20000)
    math(EXPR start "${length} - 20000")
    string(SUBSTRING "${text}" ${start} -1 text)
  endif()
  message(FATAL_ERROR "${text}\nThe emulator in ${BUILD_DIR} did not build (${log})")
endif()
