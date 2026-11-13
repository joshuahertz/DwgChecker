# dwgcore: all comparison logic. Depends on QtCore only (no GUI, no widgets),
# so the unit tests exercise exactly what the GUI runs. Zips are read with zlib: Qt's
# own copy inside QtCore where Qt has one (Qt6::ZlibPrivate, Windows), else the
# system's (Linux, macOS), the one QtCore itself uses there (D81, D93).
add_library(dwgcore STATIC
  Geometry.h Geometry.cpp
  Model.h Model.cpp
  Color.h Color.cpp
  Settings.h Settings.cpp
  DwgJson.h DwgJson.cpp
  Loader.h Loader.cpp
  MetaExtractor.h MetaExtractor.cpp
  Canonicalizer.h Canonicalizer.cpp
  DimText.h DimText.cpp
  SpatialGrid.h
  Dsu.h
  Matcher.h Matcher.cpp
  HatchCheck.h HatchCheck.cpp
  Aligner.h Aligner.cpp
  Ortho.h Ortho.cpp
  Comparator.h Comparator.cpp
  Duplicates.h Duplicates.cpp
  StudentIdentity.h StudentIdentity.cpp
  Archive.h Archive.cpp
  Batch.h Batch.cpp
  ClassSummary.h ClassSummary.cpp
  Csv.h Csv.cpp
  Format.h Format.cpp
)
target_include_directories(dwgcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(TARGET Qt6::ZlibPrivate)
  set(_dwgcore_zlib Qt6::ZlibPrivate)
else()
  find_package(ZLIB REQUIRED)
  set(_dwgcore_zlib ZLIB::ZLIB)
endif()
target_link_libraries(dwgcore PUBLIC Qt6::Core PRIVATE ${_dwgcore_zlib})
target_compile_definitions(dwgcore PUBLIC
  DWGCHECK_VERSION="${PROJECT_VERSION}"
  DWGCHECK_LIBREDWG_VERSION="${DWGCHECK_LIBREDWG_VERSION}")
