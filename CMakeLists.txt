cmake_minimum_required(VERSION 3.17)
project(SdlBase)


set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS_RELEASE "/O2") # When using MSVC compiler

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")


include_directories(.)
include_directories(src)
include_directories(./lib/SDL2-devel-2.0.9-VC/SDL2-2.0.9/include)
link_directories(./lib/SDL2-devel-2.0.9-VC/SDL2-2.0.9/lib)

add_executable(SdlBase
        src/_EntryPoint_.cpp
        src/gui_core/BinHeap.cpp
        src/gui_core/BinHeap.h
        src/gui_core/ImmediateDraw.cpp
        src/gui_core/ImmediateDraw.h
        src/gui_core/ScanBufferDraw.cpp
        src/gui_core/ScanBufferDraw.h
        src/gui_core/ScanBufferFont.cpp
        src/gui_core/ScanBufferFont.h
        src/gui_core/Sort.cpp
        src/gui_core/Sort.h)

configure_file(lib/SDL2-devel-2.0.9-VC/SDL2-2.0.9/lib/x86/SDL2.dll SDL2.dll COPYONLY)
target_link_libraries(SdlBase "${PROJECT_SOURCE_DIR}/lib/SDL2-devel-2.0.9-VC/SDL2-2.0.9/lib/x86/SDL2.lib")

