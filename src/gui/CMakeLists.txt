# App icon: drawn by packaging/icon/make_icon.ps1, packed by make_ico.py. The .rc
# puts it on DwgChecker.exe (Explorer, taskbar); the PNGs are the window icon.
set(_app_icon_sources "")
if(WIN32)
  set(_app_icon_sources resources/app.rc)
endif()
qt_add_executable(DwgChecker WIN32 MACOSX_BUNDLE
  ${_app_icon_sources}
  main.cpp
  MainWindow.h MainWindow.cpp
  SettingsPanel.h SettingsPanel.cpp
  ResultsModel.h ResultsModel.cpp
  DuplicatesTab.h DuplicatesTab.cpp
  PathCard.h PathCard.cpp
  DetailDialog.h DetailDialog.cpp
  DrawingView.h DrawingView.cpp
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

# windeployqt / macdeployqt at install time. Qt 6.5 renamed FILENAME_VARIABLE to OUTPUT_SCRIPT.
# NO_COMPILER_RUNTIME: the runtime DLLs are installed above, so windeployqt adds no
# vc_redist.x64.exe.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
  qt_generate_deploy_app_script(TARGET DwgChecker OUTPUT_SCRIPT _deploy NO_UNSUPPORTED_PLATFORM_ERROR
                                NO_COMPILER_RUNTIME)
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
