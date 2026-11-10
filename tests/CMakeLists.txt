# QtTest unit tests. All but tst_dwgread run on the committed JSON fixtures, so
# they need neither dwgread nor AutoCAD.
function(dwgcheck_add_test name)
  add_executable(${name} ${name}.cpp TestUtil.h)
  target_link_libraries(${name} PRIVATE dwgcore Qt6::Test)
  target_compile_definitions(${name} PRIVATE DWGCHECK_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  add_test(NAME ${name} COMMAND ${name})
  if(WIN32)
    # Qt6Test.dll is not part of the deployed app; let ctest find it in the Qt kit.
    set_tests_properties(${name} PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::qmake>")
  endif()
endfunction()

dwgcheck_add_test(tst_canon)
dwgcheck_add_test(tst_corpus)
dwgcheck_add_test(tst_ortho)
dwgcheck_add_test(tst_duplicates)
dwgcheck_add_test(tst_misc)
dwgcheck_add_test(tst_hatch)
dwgcheck_add_test(tst_dims)
dwgcheck_add_test(tst_batch)

# End to end with the real parser (skips itself when no dwgread is configured).
dwgcheck_add_test(tst_dwgread)
target_compile_definitions(tst_dwgread PRIVATE
  DWGCHECK_DWGREAD_PATH="${DWGCHECK_DWGREAD}"
  DWGCHECK_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/corpus/dwg")
