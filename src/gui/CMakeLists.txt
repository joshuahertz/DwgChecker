qt_add_executable(DwgChecker WIN32 MACOSX_BUNDLE
  main.cpp
  MainWindow.h MainWindow.cpp
  SettingsPanel.h SettingsPanel.cpp
  ResultsModel.h ResultsModel.cpp
  DuplicatesTab.h DuplicatesTab.cpp
  DetailDialog.h DetailDialog.cpp
  DrawingView.h DrawingView.cpp
  ViewEditorDialog.h ViewEditorDialog.cpp
  AutoCadController.h AutoCadController.cpp
)
target_link_libraries(DwgChecker PRIVATE dwgcore dwgplatform Qt6::Widgets Qt6::Concurrent)

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

# windeployqt / macdeployqt at install time.
qt_generate_deploy_app_script(TARGET DwgChecker OUTPUT_SCRIPT _deploy NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${_deploy})
