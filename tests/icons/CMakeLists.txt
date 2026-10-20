## icons.generated: the icon files made from the approved PNGs are the ones committed ##
#
#	nulldc3/icon/make_icons.py --check: the eleven PNGs of nulldc3/icon/png/ are valid inputs (signature, chunks,
#	IHDR size equal to the name's, 8-bit RGBA, not interlaced, the image data inflated and every row filter
#	undone), the three files the script makes from them (nulldc3/windows/nulldc3.ico, nulldc3/apple/nulldc3.icns,
#	libswirl/linux-dist/x11_icon.h) are byte for byte the committed ones, and the committed .ico and .icns are well
#	formed (count, sizes, offsets, lengths, a PNG at every offset). It writes nothing; a file that differs or is
#	broken is named. nulldc3/icon/README.md has how the files are made.
#
#	Needs python3 (3.9 or later, the standard library): without it the test is skipped, a failure with
#	NULLDC_TESTS_REQUIRE_DATA.
#
## icons.x11: the values of the X11 window's icon ##
#
#	x11_icon_cardinals() (libswirl/linux-dist/x11_icon.cpp, which includes no X11 header), what the X11 window gets
#	as its _NET_WM_ICON: the header's uint32_t values zero-extended to unsigned long, the six blocks of 16 to 128
#	px with their width and height, the 64-bit FNV-1a of the values equal to the header's, the corners transparent
#	and the centre opaque. No display.
#
#	Needs no data.
#

find_package(Python3 COMPONENTS Interpreter)

if(NOT Python3_Interpreter_FOUND)
  nulldc_add_missing_data_test(icons.generated "no python3 to run nulldc3/icon/make_icons.py --check with")
else()
  add_test(NAME icons.generated
           COMMAND "${Python3_EXECUTABLE}" "${nulldc_root_path}/nulldc3/icon/make_icons.py" --check)
  set_tests_properties(icons.generated PROPERTIES TIMEOUT 120)
endif()

nulldc_add_libswirl_library(nulldc_x11_icon
  SOURCES linux-dist/x11_icon.cpp
)

add_executable(icons_x11 x11_icon_test.cpp)
target_link_libraries(icons_x11 PRIVATE nulldc_x11_icon)
if(WIN32)
  # the policy of the tests project for a program that dies (../common/win_policy.cpp: no box, no crash report, one line and 128 + the signal's number)
  target_sources(icons_x11 PRIVATE "${nulldc_tests_path}/common/win_policy.cpp")
endif()

add_test(NAME icons.x11 COMMAND icons_x11)
set_tests_properties(icons.x11 PROPERTIES TIMEOUT 60)
