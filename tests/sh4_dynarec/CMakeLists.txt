## SH4 single-step vectors through the dynarec ##
#
#	Nothing is built here yet.
#
#	What is there to build on (tests/common):
#	  nulldc_add_libswirl_library(<target> SOURCES ${NULLDC_SH4_INTERP_SOURCES} <dynarec sources>)
#	                        the SH4 core with the recompiler of the emulator build (FEAT_SHREC as configured)
#	  nulldc_test_common    stubs (sh4_stubs_rec.cpp is the recompiler's share), ss_suite.h runner with
#	                        SsSuite::ordered_fetches = false, ss_mem_serve_opcodes() around block decoding and
#	                        SsSuite::known for what only the dynarec fails
#	  NULLDC_HAVE_SH4_TESTS, NULLDC_SH4_TESTS_DIR, NULLDC_SH4_TESTS_MISSING, nulldc_add_skipped_test()
#
