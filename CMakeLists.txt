cmake_minimum_required(VERSION 3.16)
project(fa18-interceptor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
include(GNUInstallDirs)

add_library(m68k STATIC
  src/m68k/cpu.cpp
  src/m68k/cpu.hpp
  src/m68k/ops.hpp
)
target_include_directories(m68k PUBLIC src)

add_library(amiga STATIC
  src/amiga/machine.cpp
  src/amiga/machine.hpp
  src/amiga/custom.cpp
  src/amiga/hle.cpp
  src/amiga/hle.hpp
  src/amiga/hunk.hpp
)
target_link_libraries(amiga PUBLIC m68k)

add_executable(fa18 src/main.cpp)
target_link_libraries(fa18 PRIVATE amiga SDL2::SDL2)
target_compile_definitions(fa18 PRIVATE
  FA18_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/fa18-interceptor")

# CPU conformance test against the SingleStepTests 68000 suite:
#   ./build/cpu_sst path/to/m68000/v1/*.json.bin
add_executable(cpu_sst tests/cpu_sst.cpp)
target_link_libraries(cpu_sst PRIVATE m68k)

install(TARGETS fa18 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/fa18-interceptor)
