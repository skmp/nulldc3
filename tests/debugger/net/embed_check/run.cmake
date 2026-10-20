# One case of the devtools UI embed step, run by ctest:
#
#   cmake -DCASE=<case> -DTREE=<emulator tree> -DWORK=<scratch directory> -DGENERATOR=<generator> -P run.cmake
#
#   off         NULLDC_DEVTOOLS_DIST=OFF wins over a bundle that would be found, and gives the empty table
#   auto        the two places a bundle is looked for, their order, and what is said when there is none; an
#               uninitialised submodule devtools/ (an empty directory) and one whose UI is not built are passed over
#   pin         what configure says about the commit the bundle was built from (gitRev) and the commit the tree
#               pins for its submodule devtools/ (the gitlink): the same, another, the same with changes, none
#   mismatch    a bundle for another protocol version stops the configure step
#   incomplete  a directory named explicitly that is not a bundle stops the configure step
#   rebuilt     a rebuilt bundle (other asset names, other version) is embedded by the next build
#   corrupt     files that do not match the bundle's own manifest stop the build
#   names       hidden files are left out, a file name that cannot be embedded stops the build

set(project "${CMAKE_CURRENT_LIST_DIR}")
set(build "${WORK}/build")
set(data "${build}/dbg_webui_data.cpp")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# A bundle with one content-named asset. `manifest` is "sha256" for a manifest with hashes, "none" for
# no bundle-info.json at all.
function(write_bundle dir protocol version tag manifest)
  file(REMOVE_RECURSE "${dir}")
  file(WRITE "${dir}/index.html" "<!doctype html><script type=\"module\" src=\"./assets/app-${tag}.js\"></script>\n")
  file(WRITE "${dir}/assets/app-${tag}.js" "console.log(\"${tag}\");\n")
  if(manifest STREQUAL "sha256")
    file(SHA256 "${dir}/index.html" index_hash)
    file(SHA256 "${dir}/assets/app-${tag}.js" app_hash)
    file(WRITE "${dir}/bundle-info.json" "{\"version\": \"${version}\", \"protocol\": ${protocol}, \"gitRev\": \"${tag}\", \"files\": [
  {\"path\": \"index.html\", \"sha256\": \"${index_hash}\"},
  {\"path\": \"assets/app-${tag}.js\", \"sha256\": \"${app_hash}\"}]}\n")
  endif()
endfunction()

# Both set rc and output in the caller's scope. CMake breaks long messages into lines wherever it likes,
# so the output is kept with every run of white space as one space.
function(configure tree)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -G "${GENERATOR}" -S "${project}" -B "${build}" "-DDEBUGGER_DIR=${TREE}/libswirl/debugger" "-DTREE=${tree}" ${ARGN}
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(REGEX REPLACE "[ \t\r\n]+" " " out "${out}${err}")
  set(rc "${rc}" PARENT_SCOPE)
  set(output "${out}" PARENT_SCOPE)
endfunction()

function(build)
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(REGEX REPLACE "[ \t\r\n]+" " " out "${out}${err}")
  set(rc "${rc}" PARENT_SCOPE)
  set(output "${out}" PARENT_SCOPE)
endfunction()

function(expect_success what)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${CASE}: ${what} failed:\n${output}")
  endif()
endfunction()

function(expect_failure what)
  if(rc EQUAL 0)
    message(FATAL_ERROR "${CASE}: ${what} succeeded, it must not:\n${output}")
  endif()
endfunction()

function(expect_output pattern)
  if(NOT output MATCHES "${pattern}")
    message(FATAL_ERROR "${CASE}: expected \"${pattern}\" in:\n${output}")
  endif()
endfunction()

function(expect_data pattern)
  file(READ "${data}" text)
  if(NOT text MATCHES "${pattern}")
    message(FATAL_ERROR "${CASE}: expected \"${pattern}\" in ${data}")
  endif()
endfunction()

function(expect_no_data pattern)
  file(READ "${data}" text)
  if(text MATCHES "${pattern}")
    message(FATAL_ERROR "${CASE}: \"${pattern}\" must not be in ${data}")
  endif()
endfunction()

if(CASE STREQUAL "off")
  write_bundle("${WORK}/tree/devtools/dist-native" 3 "1.0.0" "aaaa1111" sha256)
  configure("${WORK}/tree" -DNULLDC_DEVTOOLS_DIST=OFF)
  expect_success("configure")
  expect_output("Dreamcast DevTools: not embedded \\(NULLDC_DEVTOOLS_DIST=OFF\\)")
  build()
  expect_success("build")
  expect_data("const File kFiles\\[\\] = {\n\t{ nullptr, nullptr, 0, 0 }\n};")
  expect_data("const char kVersion\\[\\] = \"\";")
  expect_no_data("file0")

elseif(CASE STREQUAL "auto")
  file(MAKE_DIRECTORY "${WORK}/top/tree")
  configure("${WORK}/top/tree")
  expect_success("configure without a bundle")
  expect_output("Dreamcast DevTools: not embedded \\(no bundle found, set NULLDC_DEVTOOLS_DIST")

  # the submodule devtools/ of a clone that did not initialise it is an empty directory; one that did and has not
  # built the UI has its sources and no dist-native/index.html. Neither is a bundle, and neither hides the next place
  file(MAKE_DIRECTORY "${WORK}/top/tree/devtools")
  configure("${WORK}/top/tree")
  expect_success("configure with an empty devtools directory")
  expect_output("Dreamcast DevTools: not embedded \\(no bundle found, set NULLDC_DEVTOOLS_DIST")

  # next to the tree, as a checkout of both repositories has it; not a bundle until the UI build says what it is
  write_bundle("${WORK}/top/dreamcast-devtools/dist-native" 3 "1.2.3" "aaaa1111" none)
  configure("${WORK}/top/tree")
  expect_success("configure with a bundle that has no bundle-info.json")
  expect_output("Dreamcast DevTools: not embedded \\(.*/top/dreamcast-devtools/dist-native has no bundle-info.json\\)")

  write_bundle("${WORK}/top/dreamcast-devtools/dist-native" 3 "1.2.3" "aaaa1111" sha256)
  configure("${WORK}/top/tree")
  expect_success("configure with the bundle next to the tree")
  expect_output("Dreamcast DevTools: embedding .*/top/dreamcast-devtools/dist-native \\(1.2.3, protocol 3\\)")
  file(WRITE "${WORK}/top/tree/devtools/package.json" "{}\n")
  file(MAKE_DIRECTORY "${WORK}/top/tree/devtools/dist-native")
  configure("${WORK}/top/tree")
  expect_success("configure with a devtools submodule whose UI is not built")
  expect_output("Dreamcast DevTools: embedding .*/top/dreamcast-devtools/dist-native \\(1.2.3, protocol 3\\)")
  build()
  expect_success("build")
  # its size as written: file(WRITE) ends the line with \r\n on Windows
  file(SIZE "${WORK}/top/dreamcast-devtools/dist-native/assets/app-aaaa1111.js" app_size)
  expect_data("\"assets/app-aaaa1111.js\", file0, sizeof\\(file0\\), ${app_size} }")
  expect_data("\"bundle-info.json\", file1")
  expect_data("\"index.html\", file2")
  expect_data("const char kVersion\\[\\] = \"1.2.3\";")
  expect_data("const char kGitRev\\[\\] = \"aaaa1111\";")

  # inside the tree: found first
  write_bundle("${WORK}/top/tree/devtools/dist-native" 3 "4.5.6" "bbbb2222" sha256)
  configure("${WORK}/top/tree")
  expect_success("configure with a bundle inside the tree")
  expect_output("Dreamcast DevTools: embedding .*/top/tree/devtools/dist-native \\(4.5.6, protocol 3\\)")
  build()
  expect_success("build")
  expect_data("const char kVersion\\[\\] = \"4.5.6\";")
  expect_no_data("aaaa1111")

  # a bundle that is found is checked like one that is named
  write_bundle("${WORK}/top/tree/devtools/dist-native" 2 "0.9.0" "cccc3333" sha256)
  configure("${WORK}/top/tree")
  expect_failure("configure with an old bundle inside the tree")
  expect_output("is built for protocol 2, the debugger speaks protocol 3")
  expect_output("NULLDC_DEVTOOLS_DIST=OFF")

elseif(CASE STREQUAL "pin")
  find_program(git NAMES git)
  if(NOT git)
    message(FATAL_ERROR "${CASE}: no git to make a tree with a submodule")
  endif()
  set(tree "${WORK}/tree")
  set(pin "0123456789abcdef0123456789abcdef01234567")
  execute_process(COMMAND "${git}" init -q "${tree}" RESULT_VARIABLE rc)
  execute_process(COMMAND "${git}" -C "${tree}" update-index --add --cacheinfo "160000,${pin},devtools" RESULT_VARIABLE rc2)
  if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0)
    message(FATAL_ERROR "${CASE}: git could not make the tree")
  endif()
  write_bundle("${tree}/devtools/dist-native" 3 "1.0.0" "${pin}" sha256)
  configure("${tree}")
  expect_success("configure with the bundle of the pinned commit")
  expect_output("Dreamcast DevTools: the bundle was built from the submodule's commit ${pin}")

  write_bundle("${tree}/devtools/dist-native" 3 "1.0.0" "fedcba9876543210fedcba9876543210fedcba98" sha256)
  configure("${tree}")
  expect_success("configure with the bundle of another commit")
  expect_output("Dreamcast DevTools: the bundle was built from fedcba9876543210fedcba9876543210fedcba98, another commit than the submodule devtools, ${pin}")

  write_bundle("${tree}/devtools/dist-native" 3 "1.0.0" "${pin}-dirty" sha256)
  configure("${tree}")
  expect_success("configure with the bundle of the pinned commit and changes")
  expect_output("the bundle was built from the submodule's commit ${pin}, with uncommitted changes")

  # a bundle that names no commit: the UI built without git ("") or a fixture's name
  write_bundle("${tree}/devtools/dist-native" 3 "1.0.0" "fixture" sha256)
  configure("${tree}")
  expect_success("configure with a bundle that names no commit")
  expect_output("Dreamcast DevTools: the bundle does not say which commit it was built from \\(gitRev 'fixture'\\); the submodule devtools is at ${pin}")

  # a tree that is no git checkout pins nothing
  write_bundle("${WORK}/plain/devtools/dist-native" 3 "1.0.0" "${pin}" sha256)
  configure("${WORK}/plain")
  expect_success("configure in a tree that is no git checkout")
  expect_output("Dreamcast DevTools: the tree pins no commit for devtools/")

elseif(CASE STREQUAL "mismatch")
  write_bundle("${WORK}/bundle" 2 "0.9.0" "aaaa1111" sha256)
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_failure("configure")
  expect_output("Dreamcast DevTools: .*/bundle is built for protocol 2, the debugger speaks protocol 3")
  write_bundle("${WORK}/bundle" 4 "9.0.0" "aaaa1111" sha256)
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_failure("configure")
  expect_output("is built for protocol 4, the debugger speaks protocol 3")

elseif(CASE STREQUAL "incomplete")
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/nowhere")
  expect_failure("configure with a directory that does not exist")
  expect_output("is not a bundle")
  write_bundle("${WORK}/bundle" 3 "1.0.0" "aaaa1111" none)
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_failure("configure with a directory without bundle-info.json")
  expect_output("is not a bundle, .*/bundle needs index.html and bundle-info.json")
  file(WRITE "${WORK}/bundle/bundle-info.json" "{\"version\": \"1.0.0\"}")
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_failure("configure with a bundle-info.json without a protocol")
  expect_output("does not give \"version\" and \"protocol\"")
  file(WRITE "${WORK}/bundle/bundle-info.json" "not json")
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_failure("configure with a bundle-info.json that is not JSON")

elseif(CASE STREQUAL "rebuilt")
  write_bundle("${WORK}/bundle" 3 "1.0.0" "aaaa1111" sha256)
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_success("configure")
  expect_output("Dreamcast DevTools: embedding .*/bundle \\(1.0.0, protocol 3\\)")
  build()
  expect_success("build")
  expect_output("Embedding Dreamcast DevTools")
  expect_data("\"assets/app-aaaa1111.js\"")
  expect_data("const char kVersion\\[\\] = \"1.0.0\";")
  file(SHA256 "${data}" first)

  build()
  expect_success("second build")
  if(output MATCHES "Embedding Dreamcast DevTools")
    message(FATAL_ERROR "${CASE}: the embed step ran again although nothing changed:\n${output}")
  endif()

  # What a UI build does: the directory is emptied, the assets get new names, the manifest is written last.
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
  write_bundle("${WORK}/bundle" 3 "2.0.0" "bbbb2222" sha256)
  build()
  expect_success("build after the bundle changed")
  expect_output("Dreamcast DevTools: embedding .*/bundle \\(2.0.0, protocol 3\\)")
  expect_data("\"assets/app-bbbb2222.js\"")
  expect_data("const char kVersion\\[\\] = \"2.0.0\";")
  expect_no_data("aaaa1111")

  # the same bundle again gives the same bytes
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
  write_bundle("${WORK}/bundle" 3 "1.0.0" "aaaa1111" sha256)
  build()
  expect_success("build after the bundle changed back")
  file(SHA256 "${data}" again)
  if(NOT first STREQUAL again)
    message(FATAL_ERROR "${CASE}: the same bundle gave different output")
  endif()

  # a rebuilt bundle for another protocol stops the build
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
  write_bundle("${WORK}/bundle" 2 "0.9.0" "cccc3333" sha256)
  build()
  expect_failure("build after the bundle changed its protocol")
  expect_output("is built for protocol 2, the debugger speaks protocol 3")

elseif(CASE STREQUAL "corrupt")
  write_bundle("${WORK}/bundle" 3 "1.0.0" "aaaa1111" sha256)
  file(APPEND "${WORK}/bundle/assets/app-aaaa1111.js" "// changed after the manifest was written\n")
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_success("configure")
  build()
  expect_failure("build with a file that does not match the manifest")
  expect_output("assets/app-aaaa1111.js does not match its sha256 in bundle-info.json")

  file(REMOVE_RECURSE "${build}")
  write_bundle("${WORK}/bundle" 3 "1.0.0" "aaaa1111" sha256)
  file(REMOVE "${WORK}/bundle/assets/app-aaaa1111.js")
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_success("configure")
  build()
  expect_failure("build with a listed file missing")
  expect_output("assets/app-aaaa1111.js is listed in bundle-info.json but missing")

elseif(CASE STREQUAL "names")
  # what a file manager or a bundler leaves behind: hidden files and directories
  write_bundle("${WORK}/bundle" 3 "1.0.0" "aaaa1111" sha256)
  file(WRITE "${WORK}/bundle/.DS_Store" "x")
  file(WRITE "${WORK}/bundle/.vite/manifest.json" "{}")
  file(WRITE "${WORK}/bundle/assets/.cache/chunk-cccc3333.js" "x")
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_success("configure")
  build()
  expect_success("build with hidden files in the bundle")
  expect_data("\"assets/app-aaaa1111.js\", file0")
  expect_data("\"bundle-info.json\", file1")
  expect_data("\"index.html\", file2")
  expect_no_data("file3")
  expect_no_data("DS_Store")
  expect_no_data("manifest")
  expect_no_data("cccc3333")

  # a name that cannot be written into the table
  file(REMOVE_RECURSE "${build}")
  write_bundle("${WORK}/bundle" 3 "1.0.0" "aaaa1111" sha256)
  file(WRITE "${WORK}/bundle/assets/odd[1].js" "x")
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_success("configure")
  build()
  expect_failure("build with a file name that cannot be embedded")
  expect_output("assets/odd\\[1\\].js: this file name cannot be embedded")

  # a ';' separates the entries where the files are listed: in a file name, in a directory name
  file(REMOVE_RECURSE "${build}")
  write_bundle("${WORK}/bundle" 3 "1.0.0" "aaaa1111" sha256)
  file(WRITE "${WORK}/bundle/assets/semi;colon.js" "x")
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_success("configure")
  build()
  expect_failure("build with a ';' in a file name")
  expect_output("assets/semi;colon.js: this file name cannot be embedded")

  file(REMOVE_RECURSE "${build}")
  write_bundle("${WORK}/bundle" 3 "1.0.0" "aaaa1111" sha256)
  file(WRITE "${WORK}/bundle/semi;colon/app.js" "x")
  configure("${WORK}/tree" "-DNULLDC_DEVTOOLS_DIST=${WORK}/bundle")
  expect_success("configure")
  build()
  expect_failure("build with a ';' in a directory name")
  expect_output("this file name cannot be embedded")

else()
  message(FATAL_ERROR "unknown case '${CASE}'")
endif()

message("embed_${CASE}: ok")
