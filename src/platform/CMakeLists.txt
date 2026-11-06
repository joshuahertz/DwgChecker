# dwgplatform: finding/launching AutoCAD and locating dwgread. All
# platform-specific code is in win/ and mac/.
add_library(dwgplatform STATIC Platform.h Platform.cpp)
target_include_directories(dwgplatform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dwgplatform PUBLIC Qt6::Core)
if(WIN32)
  target_sources(dwgplatform PRIVATE win/AutoCadWin.cpp)
  target_link_libraries(dwgplatform PRIVATE ole32 oleaut32 user32)
elseif(APPLE)
  target_sources(dwgplatform PRIVATE mac/AutoCadMac.cpp)
endif()
