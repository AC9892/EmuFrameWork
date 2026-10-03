cmake_minimum_required(VERSION 3.20)
project(EmuFrame VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)

set(EF_MGBA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Emulator Master Folders/mgba-master" CACHE PATH "Path to the mGBA source checkout")
if(NOT EXISTS "${EF_MGBA_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "mGBA source is required; set EF_MGBA_SOURCE_DIR to its checkout (see README.md)")
endif()
set(LIBMGBA_ONLY ON CACHE BOOL "Build only the mGBA library" FORCE)
set(M_CORE_GB ON CACHE BOOL "Game Boy core" FORCE)
set(M_CORE_GBA ON CACHE BOOL "Game Boy Advance core" FORCE)
set(USE_ZLIB OFF CACHE BOOL "Avoid external zlib" FORCE)
set(USE_PNG OFF CACHE BOOL "Avoid external PNG" FORCE)
add_subdirectory("${EF_MGBA_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/mgba-master" EXCLUDE_FROM_ALL)

add_library(EmuFrameCore SHARED
  src/api/emuframe.cpp
  src/core/instance.cpp
  src/core/rom.cpp
  src/core/sha256.cpp
  src/library/library.cpp
  backends/mgba/mgba_backend.cpp)
target_compile_definitions(EmuFrameCore PRIVATE EMUFRAME_BUILD ENABLE_VFS ENABLE_DIRECTORIES BUILD_STATIC M_CORE_GB M_CORE_GBA)
target_include_directories(EmuFrameCore PUBLIC include PRIVATE src backends/mgba)
target_link_libraries(EmuFrameCore PRIVATE mgba)
set_target_properties(EmuFrameCore PROPERTIES OUTPUT_NAME EmuFrameCore)

add_executable(EmuFrameEmbedExample examples/minimal_host.cpp)
target_link_libraries(EmuFrameEmbedExample PRIVATE EmuFrameCore)
if(WIN32)
  target_link_libraries(EmuFrameEmbedExample PRIVATE comdlg32)
endif()

if(WIN32)
  add_executable(EmuFrameTestHost WIN32 apps/test_host/win32_main.cpp apps/test_host/settings.cpp)
  target_link_libraries(EmuFrameTestHost PRIVATE EmuFrameCore comdlg32 winmm shell32 xinput9_1_0)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(EmuFrameTests tests/basic.cpp apps/test_host/settings.cpp)
  target_link_libraries(EmuFrameTests PRIVATE EmuFrameCore)
  add_test(NAME EmuFrameTests COMMAND EmuFrameTests)
endif()
