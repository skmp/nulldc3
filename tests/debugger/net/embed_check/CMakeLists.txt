# A project that only runs the devtools UI embed step; embed_check/run.cmake configures and builds it.
#
#   cmake -S embed_check -B <dir> -DDEBUGGER_DIR=<tree>/libswirl/debugger -DTREE=<tree to look for bundles in> [-DNULLDC_DEVTOOLS_DIST=...]

cmake_minimum_required(VERSION 3.19)
project(dbg_webui_embed_check NONE)

include("${DEBUGGER_DIR}/cmake/dbg_webui.cmake")
dbg_webui_data("${TREE}" data)
add_custom_target(embed ALL DEPENDS "${data}")
