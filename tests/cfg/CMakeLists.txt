## The config store ##
#
#	cfg.overrides    cfg/cfg.cpp, cfg/ini.cpp and the command line parser cfg/cl.cpp on a config file of their own:
#	                 a value that --config gave is in effect for the run and is not what a save writes to emu.cfg,
#	                 and a read-only store (cfgSetReadOnly, the automated runs) writes nothing at all
#	cfg.log_file     cl_LogToFile of cfg/cl.cpp, what Windows' --log does: stdout and stderr into one file, made
#	                 empty first, every line of both in the order they were written, none lost to a buffer when the
#	                 process ends without a flush (POSIX hosts: the test forks)
#
#	Needs no data.
#

nulldc_add_libswirl_library(nulldc_cfg
  SOURCES cfg/cfg.cpp cfg/ini.cpp cfg/cl.cpp
)
# cl.cpp's --version prints the version
nulldc_add_version_sources(nulldc_cfg)

add_executable(cfg_overrides cfg_test.cpp)
target_link_libraries(cfg_overrides PRIVATE nulldc_cfg)
if(WIN32)
  # the policy of the tests project for a program that dies (../common/win_policy.cpp: no box, no crash report, one line and 128 + the signal's number)
  target_sources(cfg_overrides PRIVATE "${nulldc_tests_path}/common/win_policy.cpp")
endif()

add_test(NAME cfg.overrides COMMAND cfg_overrides "${CMAKE_CURRENT_BINARY_DIR}/work")
set_tests_properties(cfg.overrides PROPERTIES TIMEOUT 60)

if(UNIX)
  add_executable(cfg_log log_test.cpp)
  target_link_libraries(cfg_log PRIVATE nulldc_cfg)
  add_test(NAME cfg.log_file COMMAND cfg_log "${CMAKE_CURRENT_BINARY_DIR}/log_work")
  set_tests_properties(cfg.log_file PROPERTIES TIMEOUT 60)
else()
  nulldc_add_skipped_test(cfg.log_file "the test forks: POSIX hosts only")
endif()
