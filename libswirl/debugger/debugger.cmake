# The debugger: every source under libswirl/debugger, and the devtools UI embedded into the binary.
#
# Included once from the top-level CMakeLists.txt, after core_SRCS is set and before the target is created.
# The code tests NULLDC_DEBUGGER; with the option OFF nothing under libswirl/debugger is compiled.

set(NULLDC_DEBUGGER_DEFAULT ON)
option(NULLDC_DEBUGGER "Debugger server for the devtools UI (HTTP + WebSocket)" ${NULLDC_DEBUGGER_DEFAULT})

if(NULLDC_DEBUGGER)
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/dbg_webui.cmake")

  file(GLOB debugger_SRCS ${d_core}/debugger/*.cpp ${d_core}/debugger/*.h)
  dbg_webui_data("${nulldc_root_path}" debugger_webui_SRC)
  list(APPEND core_SRCS ${debugger_SRCS} ${debugger_webui_SRC})

  add_definitions(-DNULLDC_DEBUGGER=1)

  if(${HOST_OS} EQUAL ${OS_WINDOWS})
    link_libraries(ws2_32)
  endif()
endif()
