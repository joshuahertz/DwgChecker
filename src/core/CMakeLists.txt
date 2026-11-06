# dwgcore: all comparison logic. Depends on QtCore only (no GUI, no widgets),
# so the unit tests exercise exactly what the GUI runs.
add_library(dwgcore STATIC
  Geometry.h Geometry.cpp
  Model.h Model.cpp
  Settings.h Settings.cpp
  DwgJson.h DwgJson.cpp
  Loader.h Loader.cpp
  MetaExtractor.h MetaExtractor.cpp
  Canonicalizer.h Canonicalizer.cpp
  DimText.h DimText.cpp
  SpatialGrid.h
  Matcher.h Matcher.cpp
  HatchCheck.h HatchCheck.cpp
  Aligner.h Aligner.cpp
  Ortho.h Ortho.cpp
  Comparator.h Comparator.cpp
  Duplicates.h Duplicates.cpp
  StudentIdentity.h StudentIdentity.cpp
  Batch.h Batch.cpp
  Csv.h Csv.cpp
  Format.h Format.cpp
)
target_include_directories(dwgcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dwgcore PUBLIC Qt6::Core)
target_compile_definitions(dwgcore PUBLIC
  DWGCHECK_VERSION="${PROJECT_VERSION}"
  DWGCHECK_LIBREDWG_VERSION="${DWGCHECK_LIBREDWG_VERSION}")
