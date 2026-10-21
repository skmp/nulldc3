# The last step of every build that changes nulldc3.app (CMakeLists.txt: the target nulldc3_bundle; PL-D42):
#
#   cmake -DBUNDLE=<build>/nulldc3.app -DVERSION_FILE=<build>/nulldc3-version.cmake [-DSIGN=OFF]
#         [-DSTAMP=<build>/nulldc3-bundle.stamp] -P bundle_stamp.cmake
#
# It writes the three version keys of the build into the bundle's Info.plist (CMake writes the plist from
# Info.plist.in when it generates the build, with placeholders there): CFBundleShortVersionString X.Y.Z,
# CFBundleVersion X.Y.Z.N (four parts, PL-D29) and NullDCVersion the version, from nulldc3-version.cmake (made by
# nulldc3/cmake/version.cmake at every build). Then it signs the bundle ad hoc, without the hardened runtime and
# without entitlements, and verifies the signature; nothing touches the bundle after it. Last the stamp file, whose
# time the build compares with what the step depends on: the executable, nulldc3-version.cmake, Info.plist.in, the
# bundle's Info.plist and every resource of the bundle. SIGN=OFF (the Xcode generator, which signs itself) writes
# the keys alone.

cmake_minimum_required(VERSION 3.16)

if(NOT BUNDLE OR NOT VERSION_FILE)
  message(FATAL_ERROR "bundle_stamp.cmake: needs -DBUNDLE=<nulldc3.app> -DVERSION_FILE=<nulldc3-version.cmake>")
endif()
if(NOT DEFINED SIGN)
  set(SIGN ON)
endif()

include("${VERSION_FILE}")
if(NOT NULLDC_VERSION OR NOT NULLDC_VERSION_SHORT OR NOT NULLDC_VERSION_DOTTED)
  message(FATAL_ERROR "bundle_stamp.cmake: ${VERSION_FILE} gives no NULLDC_VERSION, NULLDC_VERSION_SHORT or NULLDC_VERSION_DOTTED")
endif()

set(plist "${BUNDLE}/Contents/Info.plist")
foreach(entry "CFBundleShortVersionString=${NULLDC_VERSION_SHORT}" "CFBundleVersion=${NULLDC_VERSION_DOTTED}"
              "NullDCVersion=${NULLDC_VERSION}")
  string(REGEX MATCH "^([^=]+)=(.*)$" _ "${entry}")
  execute_process(COMMAND plutil -replace "${CMAKE_MATCH_1}" -string "${CMAKE_MATCH_2}" "${plist}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "bundle_stamp.cmake: plutil -replace ${CMAKE_MATCH_1} ${plist}: exit ${rc} ${out}${err}")
  endif()
endforeach()

if(SIGN)
  execute_process(COMMAND codesign --force --sign - --timestamp=none "${BUNDLE}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "bundle_stamp.cmake: codesign --sign - ${BUNDLE}: exit ${rc} ${out}${err}")
  endif()
  execute_process(COMMAND codesign --verify --strict --deep "${BUNDLE}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "bundle_stamp.cmake: codesign --verify --strict --deep ${BUNDLE}: exit ${rc} ${out}${err}")
  endif()
endif()

message(STATUS "nulldc3.app: ${NULLDC_VERSION} (${NULLDC_VERSION_SHORT}, ${NULLDC_VERSION_DOTTED})")
if(STAMP)
  file(TOUCH "${STAMP}")
endif()
