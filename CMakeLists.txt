cmake_minimum_required(VERSION 3.16)
project(fa18-interceptor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
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
  src/recomp/recomp.hpp
  src/recomp/runtime.hpp
)
target_link_libraries(amiga PUBLIC m68k)

# Static recompilation of the game code (tools/recomp.py) at build time.
option(FA18_RECOMP "Build with statically recompiled game code" ON)
set(FA18_EXE "${CMAKE_SOURCE_DIR}/assets/F-18 Interceptor")
if(FA18_RECOMP)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(RECOMP_DIR ${CMAKE_BINARY_DIR}/recomp)
  set(RECOMP_SRCS ${RECOMP_DIR}/entries.cpp)
  foreach(i RANGE 0 15)
    if(i LESS 10)
      list(APPEND RECOMP_SRCS ${RECOMP_DIR}/code_0${i}.cpp)
    else()
      list(APPEND RECOMP_SRCS ${RECOMP_DIR}/code_${i}.cpp)
    endif()
  endforeach()
  add_custom_command(
    OUTPUT ${RECOMP_SRCS}
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/recomp.py ${FA18_EXE} ${RECOMP_DIR}
            --seeds ${CMAKE_SOURCE_DIR}/tools/recomp_seeds.txt
    DEPENDS ${FA18_EXE} ${CMAKE_SOURCE_DIR}/tools/recomp.py ${CMAKE_SOURCE_DIR}/tools/loadseg.py
            ${CMAKE_SOURCE_DIR}/tools/hunks.py ${CMAKE_SOURCE_DIR}/tools/recomp_seeds.txt
    COMMENT "Recompiling 68000 game code to C++")
  add_library(recomp STATIC ${RECOMP_SRCS})
  # The generated functions are huge goto-based state machines; -O1 keeps
  # build times reasonable while still producing fast code.
  target_compile_options(recomp PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-O1 -w>)
else()
  add_library(recomp STATIC src/recomp/stub.cpp)
endif()
target_include_directories(recomp PUBLIC src)
target_link_libraries(recomp PUBLIC m68k)
target_link_libraries(amiga PUBLIC recomp)

# Hand-translated C++ versions of game routines. OBJECT library so the
# static registrations are always linked in.
file(GLOB NATIVE_SRCS CONFIGURE_DEPENDS src/native/*.cpp src/native/*.hpp)
add_library(native OBJECT ${NATIVE_SRCS})
target_link_libraries(native PUBLIC amiga)

# Line coverage of the native translation (tests/coverage_native.sh):
#   cmake -B build-cov -DFA18_COVERAGE=ON
option(FA18_COVERAGE "Instrument src/native for gcov" OFF)
if(FA18_COVERAGE)
  target_compile_options(native PRIVATE --coverage -O0)
endif()

add_executable(fa18 src/main.cpp $<TARGET_OBJECTS:native>)
if(FA18_COVERAGE)
  target_link_options(fa18 PRIVATE --coverage)
endif()
target_link_libraries(fa18 PRIVATE amiga SDL2::SDL2)
target_compile_definitions(fa18 PRIVATE
  FA18_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/fa18-interceptor")

# CPU conformance test against the SingleStepTests 68000 suite:
#   ./build/cpu_sst path/to/m68000/v1/*.json.bin
add_executable(cpu_sst tests/cpu_sst.cpp)
target_link_libraries(cpu_sst PRIVATE m68k)

install(TARGETS fa18 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/fa18-interceptor)
