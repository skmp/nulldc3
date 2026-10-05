## The config store ##
#
#	cfg.overrides    cfg/cfg.cpp, cfg/ini.cpp and the command line parser cfg/cl.cpp on a config file of their own:
#	                 a value that -config gave is in effect for the run and is not what a save writes to emu.cfg,
#	                 and a read-only store (cfgSetReadOnly, the automated runs) writes nothing at all
#
#	Needs no data.
#

nulldc_add_libswirl_library(nulldc_cfg
  SOURCES cfg/cfg.cpp cfg/ini.cpp cfg/cl.cpp
)

add_executable(cfg_overrides cfg_test.cpp)
target_link_libraries(cfg_overrides PRIVATE nulldc_cfg)

add_test(NAME cfg.overrides COMMAND cfg_overrides "${CMAKE_CURRENT_BINARY_DIR}/work")
set_tests_properties(cfg.overrides PROPERTIES TIMEOUT 60)
