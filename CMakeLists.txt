cmake_minimum_required(VERSION 3.22)

project(DwgChecker VERSION 0.1.0 DESCRIPTION "Grades CAD assignments against a reference DWG" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DWGCHECK_BUILD_GUI   "Build the Qt Widgets GUI"   ON)
option(DWGCHECK_BUILD_TESTS "Build the unit tests"        ON)
set(DWGCHECK_DWGREAD "" CACHE FILEPATH
    "dwgread executable to bundle next to the app (output of scripts/build_dwgread.sh)")

set(_qt_components Core)
if(DWGCHECK_BUILD_GUI)
  list(APPEND _qt_components Gui Widgets Concurrent)
endif()
if(DWGCHECK_BUILD_TESTS)
  list(APPEND _qt_components Test)
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS ${_qt_components})
# Also puts every executable (the app and the tests) in the top of the build folder.
qt_standard_project_setup()

# The pinned LibreDWG version lives in exactly one place.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libredwg.version" _ldwg REGEX "^LIBREDWG_VERSION=")
string(REGEX REPLACE "^LIBREDWG_VERSION=" "" DWGCHECK_LIBREDWG_VERSION "${_ldwg}")

# ---- dwgread to bundle --------------------------------------------------------
# Explicit DWGCHECK_DWGREAD, else $ENV{DWGCHECK_DWGREAD}, else the output of
# scripts/build_dwgread.sh (build-dwgread/), else ~/tools/libredwg-<pinned version>/.
if(NOT DWGCHECK_DWGREAD)
  set(_dwgread_candidates
      "$ENV{DWGCHECK_DWGREAD}"
      "${CMAKE_CURRENT_SOURCE_DIR}/build-dwgread/dwgread${CMAKE_EXECUTABLE_SUFFIX}"
      "$ENV{USERPROFILE}/tools/libredwg-${DWGCHECK_LIBREDWG_VERSION}/dwgread${CMAKE_EXECUTABLE_SUFFIX}"
      "$ENV{HOME}/tools/libredwg-${DWGCHECK_LIBREDWG_VERSION}/dwgread${CMAKE_EXECUTABLE_SUFFIX}")
  foreach(_c IN LISTS _dwgread_candidates)
    if(_c AND EXISTS "${_c}" AND NOT IS_DIRECTORY "${_c}")
      file(TO_CMAKE_PATH "${_c}" _c)
      set(DWGCHECK_DWGREAD "${_c}" CACHE FILEPATH "dwgread executable to bundle next to the app" FORCE)
      break()
    endif()
  endforeach()
endif()
# Forward slashes, also for a path given on the command line (CI passes D:\a\...): the
# path becomes a C++ string literal in tst_dwgread, where "\a" would be an escape.
if(DWGCHECK_DWGREAD)
  file(TO_CMAKE_PATH "${DWGCHECK_DWGREAD}" _dwgread_norm)
  if(NOT _dwgread_norm STREQUAL DWGCHECK_DWGREAD)
    set(DWGCHECK_DWGREAD "${_dwgread_norm}" CACHE FILEPATH "dwgread executable to bundle next to the app" FORCE)
  endif()
endif()
# The static build from build_dwgread.sh is a single file. The official prebuilt
# Windows release is dynamically linked, so its runtime DLLs travel with it.
set(DWGCHECK_DWGREAD_FILES "")
if(DWGCHECK_DWGREAD)
  get_filename_component(_dwgread_dir "${DWGCHECK_DWGREAD}" DIRECTORY)
  file(GLOB _dwgread_dlls "${_dwgread_dir}/libredwg*.dll" "${_dwgread_dir}/libiconv*.dll"
                          "${_dwgread_dir}/libpcre2*.dll" "${_dwgread_dir}/libwinpthread*.dll"
                          "${_dwgread_dir}/libgcc_s*.dll")
  set(DWGCHECK_DWGREAD_FILES "${DWGCHECK_DWGREAD}" ${_dwgread_dlls})
  message(STATUS "Bundling dwgread: ${DWGCHECK_DWGREAD}")
else()
  set(_no_dwgread_level STATUS)
  if(DWGCHECK_BUILD_GUI AND (WIN32 OR APPLE))
    set(_no_dwgread_level WARNING)
  endif()
  message(${_no_dwgread_level} "dwgread not found: the app is built without its DWG parser and cannot read "
                               ".dwg files. Set DWGCHECK_DWGREAD or run scripts/build_dwgread.sh to bundle it.")
endif()

# ---- Self-contained build folder (Windows) -------------------------------------
# After each build, windeployqt copies the Qt runtime (Qt6*.dll, platforms/,
# styles/, imageformats/, ...) next to the executables, so the build folder runs
# on its own, like a windeployqt'ed release folder.
function(dwgcheck_windeployqt target)
  if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND Qt6::windeployqt $<IF:$<CONFIG:Debug>,--debug,--release> --verbose 0 ${ARGN}
      COMMENT "windeployqt: deploying the Qt runtime into the build folder"
      VERBATIM)
  endif()
endfunction()

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /wd4251)
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(src/core)
add_subdirectory(src/platform)
if(DWGCHECK_BUILD_GUI)
  add_subdirectory(src/gui)
endif()

if(DWGCHECK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
