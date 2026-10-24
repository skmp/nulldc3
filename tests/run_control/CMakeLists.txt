## Run control ##
#
#	runctl.guest_exit.*    utils/run_control.cpp on the config store and the command line parser, with stand-ins
#	                       for the machine and the user interface: the end of the guest program ends every run
#	                       that has no user interface or that a key ends, and leaves any other run to its window
#
#	One process per case: the keys of a run are read once. Needs no data.
#

nulldc_add_libswirl_library(nulldc_run_control
  SOURCES utils/run_control.cpp
)

add_executable(run_control_guest_exit guest_exit_test.cpp)
target_link_libraries(run_control_guest_exit PRIVATE nulldc_run_control nulldc_cfg)
if(WIN32)
  # the policy of the tests project for a program that dies (../common/win_policy.cpp: no box, no crash report, one line and 128 + the signal's number)
  target_sources(run_control_guest_exit PRIVATE "${nulldc_tests_path}/common/win_policy.cpp")
endif()

foreach(case window key vblanks seconds headless)
  add_test(NAME runctl.guest_exit.${case}
           COMMAND run_control_guest_exit ${case} "${CMAKE_CURRENT_BINARY_DIR}/work/${case}")
  set_tests_properties(runctl.guest_exit.${case} PROPERTIES TIMEOUT 60)
endforeach()
