# App icon: drawn by packaging/icon/make_icon.ps1, packed by make_ico.py. The .rc
# puts it on DwgChecker.exe (Explorer, taskbar); the PNGs are the window icon.
set(_app_icon_sources "")
if(WIN32)
  set(_app_icon_sources resources/app.rc)
endif()
# The application manifest: Per-Monitor V2 DPI awareness, declared (D94). With MSVC a
# .manifest source is merged into the one the linker writes.
if(MSVC)
  list(APPEND _app_icon_sources resources/DwgChecker.manifest)
endif()
qt_add_executable(DwgChecker WIN32 MACOSX_BUNDLE
  ${_app_icon_sources}
  main.cpp
  MainWindow.h MainWindow.cpp
  SettingsPanel.h SettingsPanel.cpp
  ResultsModel.h ResultsModel.cpp
  ClassSummaryTab.h ClassSummaryTab.cpp
  GuiUtil.h
  DuplicatesTab.h DuplicatesTab.cpp
  PathCard.h PathCard.cpp
  DetailDialog.h DetailDialog.cpp
  DrawingView.h DrawingView.cpp
  PreviewDialog.h PreviewDialog.cpp
  ViewEditorDialog.h ViewEditorDialog.cpp
  AutoCadController.h AutoCadController.cpp
)
target_link_libraries(DwgChecker PRIVATE dwgcore dwgplatform Qt6::Widgets Qt6::Concurrent)
qt_add_resources(DwgChecker "app_icons" PREFIX "/" BASE resources FILES
  resources/icons/app_16.png resources/icons/app_24.png resources/icons/app_32.png resources/icons/app_48.png
  resources/icons/app_64.png resources/icons/app_128.png resources/icons/app_256.png)

set_target_properties(DwgChecker PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER org.dwgchecker.DwgChecker
  MACOSX_BUNDLE_BUNDLE_NAME "DWG Checker"
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

# dwgread travels with the app: next to DwgChecker.exe (with the prebuilt's DLLs)
# on Windows, in Contents/Helpers on macOS (signed separately, see packaging/).
if(DWGCHECK_DWGREAD AND APPLE)
  add_custom_command(TARGET DwgChecker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:DwgChecker>/Helpers"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DWGCHECK_DWGREAD}" "$<TARGET_BUNDLE_CONTENT_DIR:DwgChecker>/Helpers/dwgread"
    VERBATIM)
elseif(DWGCHECK_DWGREAD_FILES)
  add_custom_command(TARGET DwgChecker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DWGCHECK_DWGREAD_FILES} "$<TARGET_FILE_DIR:DwgChecker>/"
    COMMENT "Copying dwgread into the build folder"
    VERBATIM)
endif()

# Windows: windeployqt after each build, so the build folder holds every Qt DLL
# and plugin the app needs.
dwgcheck_windeployqt(DwgChecker "$<TARGET_FILE:DwgChecker>")

install(TARGETS DwgChecker BUNDLE DESTINATION . RUNTIME DESTINATION .)
if(DWGCHECK_DWGREAD_FILES AND NOT APPLE)
  install(PROGRAMS ${DWGCHECK_DWGREAD_FILES} DESTINATION .)
endif()

# MSVC: the C++ runtime DLLs (msvcp140.dll, vcruntime140.dll, ...) go next to the exe, so the
# unzipped folder runs on a PC that never installed the Visual C++ Redistributable. They
# come from the redist folder of the compiler in use (vcvars sets VCToolsRedistDir).
if(MSVC)
  if(NOT MSVC_REDIST_DIR AND DEFINED ENV{VCToolsRedistDir})
    file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" _redist)
    string(REGEX REPLACE "/+$" "" _redist "${_redist}")
    set(MSVC_REDIST_DIR "${_redist}" CACHE PATH "MSVC redistributable folder")
  endif()
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
  include(InstallRequiredSystemLibraries)
endif()

# Licenses and source notices: a licenses/ folder next to DwgChecker.exe, in
# Contents/Resources inside the macOS app (sealed by its signature, so before signing).
# NOTICES.txt says what is in the package, under which license, and where its source is.
# For a dwgread from build_dwgread.sh, its patches and build script (with the LibreDWG
# release, its Corresponding Source) are linked in DWG Checker's own repository at the
# commit being built (D95); without a git checkout with a GitHub origin they go along in
# the package instead, laid out like the source tree so the script runs from there.
if(APPLE)
  set(_licenses_dest "DwgChecker.app/Contents/Resources/licenses")
else()
  set(_licenses_dest "licenses")
endif()
set(_lic "${PROJECT_SOURCE_DIR}/packaging/licenses")
string(REGEX MATCH "^[0-9]+\\.[0-9]+" DWGCHECK_QT_MAJOR_MINOR "${Qt6_VERSION}")
set(DWGCHECK_NOTICE_SOURCE_LINE "")
# The repository and the commit this build is made from (D95). DWGCHECK_SOURCE_URL sets the
# repository; empty, it comes from git's "origin" when that is on GitHub.
set(DWGCHECK_SOURCE_URL "" CACHE STRING "Public web address of DWG Checker's source repository (empty: git's origin on GitHub)")
set(_src_url "${DWGCHECK_SOURCE_URL}")
set(_src_commit "")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                  OUTPUT_VARIABLE _src_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    set(_src_commit "")
  endif()
  if(NOT _src_url)
    execute_process(COMMAND "${GIT_EXECUTABLE}" remote get-url origin WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                    OUTPUT_VARIABLE _origin OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(REGEX REPLACE "\\.git$" "" _origin "${_origin}")
    if(_origin MATCHES "github\\.com[:/]([^/]+/[^/]+)$")
      set(_src_url "https://github.com/${CMAKE_MATCH_1}")
    endif()
  endif()
  # A new commit or checkout configures again, so the link follows it.
  foreach(_git_file HEAD logs/HEAD)
    if(EXISTS "${PROJECT_SOURCE_DIR}/.git/${_git_file}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/.git/${_git_file}")
    endif()
  endforeach()
endif()
set(_link_source FALSE)
if(_src_url AND _src_commit)
  set(_link_source TRUE)
endif()
if(NOT DWGCHECK_DWGREAD)
  set(DWGCHECK_NOTICE_DWGREAD "This build does not include dwgread.")
elseif(DWGCHECK_DWGREAD_PREBUILT)
  file(READ "${_lic}/dwgread-prebuilt.txt.in" DWGCHECK_NOTICE_DWGREAD)
else()
  file(READ "${_lic}/dwgread-source.txt.in" DWGCHECK_NOTICE_DWGREAD)
  if(_link_source)
    set(DWGCHECK_SOURCE_WHERE "The patches and the build script are part of DWG Checker's own source code,
at the exact version this package was built from:
  ${_src_url}/tree/${_src_commit}
(third_party/ and scripts/ there; the whole tree also downloads as
${_src_url}/archive/${_src_commit}.zip).")
  else()
    set(DWGCHECK_SOURCE_WHERE "The patches and the build script are in this folder, in libredwg-source/
(laid out the same way).")
    set(DWGCHECK_NOTICE_SOURCE_LINE "  libredwg-source/  dwgread's changes and build script")
  endif()
endif()
string(CONFIGURE "${DWGCHECK_NOTICE_DWGREAD}" DWGCHECK_NOTICE_DWGREAD @ONLY)
configure_file("${_lic}/NOTICES.txt.in" "${CMAKE_CURRENT_BINARY_DIR}/NOTICES.txt" @ONLY NEWLINE_STYLE LF)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/NOTICES.txt" "${_lic}/LGPL-3.0.txt" DESTINATION "${_licenses_dest}")
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${_licenses_dest}" RENAME GPL-3.0.txt)
if(DWGCHECK_DWGREAD AND NOT DWGCHECK_DWGREAD_PREBUILT AND NOT _link_source)
  file(GLOB _patches "${PROJECT_SOURCE_DIR}/third_party/patches/*.patch")
  install(FILES "${PROJECT_SOURCE_DIR}/third_party/libredwg.version"
          DESTINATION "${_licenses_dest}/libredwg-source/third_party")
  install(FILES ${_patches} DESTINATION "${_licenses_dest}/libredwg-source/third_party/patches")
  # A plain file (run with bash, as NOTICES.txt says), so the signed macOS bundle has no
  # executable in Resources.
  install(FILES "${PROJECT_SOURCE_DIR}/scripts/build_dwgread.sh"
          DESTINATION "${_licenses_dest}/libredwg-source/scripts")
endif()

# Microsoft Store package manifest, with the four-part version the Store needs (the last part
# is the Store's and stays 0). packaging/windows/make_msix.ps1 packs it with the installed files.
if(WIN32)
  set(_patch "${PROJECT_VERSION_PATCH}")
  if(_patch STREQUAL "")
    set(_patch 0)
  endif()
  set(DWGCHECK_MSIX_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${_patch}.0")
  configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/AppxManifest.xml.in"
                 "${CMAKE_BINARY_DIR}/msix/AppxManifest.xml" @ONLY NEWLINE_STYLE CRLF)
endif()

# windeployqt / macdeployqt at install time. Qt 6.5 renamed FILENAME_VARIABLE to OUTPUT_SCRIPT.
# NO_COMPILER_RUNTIME: the runtime DLLs are installed above, so windeployqt adds no
# vc_redist.x64.exe. The app draws with QPainter only (no OpenGL, Direct3D or Qt Quick), so
# windeployqt leaves out the D3D and DXC shader compilers and the software OpenGL (about
# 40 MB, each under its own license). The option exists in Qt 6.9; older Qt keeps them.
set(_deploy_tool_options "")
if(WIN32 AND Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
  set(_deploy_tool_options DEPLOY_TOOL_OPTIONS --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw)
endif()
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
  # NO_TRANSLATIONS: the app is in English and loads none of Qt's translations, so they are
  # dead weight in the package (D94).
  qt_generate_deploy_app_script(TARGET DwgChecker OUTPUT_SCRIPT _deploy NO_UNSUPPORTED_PLATFORM_ERROR
                                NO_COMPILER_RUNTIME NO_TRANSLATIONS ${_deploy_tool_options})
else()
  qt_generate_deploy_app_script(TARGET DwgChecker FILENAME_VARIABLE _deploy NO_UNSUPPORTED_PLATFORM_ERROR)
endif()
# Windows: the Qt DLLs, plugins/ and qt.conf go next to DwgChecker.exe, at the prefix root,
# not into the "bin" that Qt takes from GNUInstallDirs when it is found. The deploy
# script keeps a QT_DEPLOY_BIN_DIR that is already set, and runs in the same scope as this.
if(WIN32)
  install(CODE [[set(QT_DEPLOY_BIN_DIR ".")]])
endif()
install(SCRIPT ${_deploy})
