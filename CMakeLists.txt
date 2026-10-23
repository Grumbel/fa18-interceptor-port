cmake_minimum_required(VERSION 3.16)
project(fa18-interceptor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)

# GCC 10+ notes ABI changes for trivial std::pair under C++17+ (-Wpsabi).
# Harmless noise on aarch64 (R36S).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wno-psabi)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── SDL2 ────────────────────────────────────────────────────────────────
if(EMSCRIPTEN)
  # WebAssembly. SDL2 is either a static build under SDL2_ROOT (headers
  # and libSDL2.a, as nix/wasm.nix makes it, offline) or Emscripten's own
  # port, which it downloads on first use.
  if(SDL2_ROOT)
    if(NOT EXISTS "${SDL2_ROOT}/lib/libSDL2.a" OR NOT EXISTS "${SDL2_ROOT}/include/SDL2/SDL.h")
      message(FATAL_ERROR "SDL2_ROOT=${SDL2_ROOT} has no lib/libSDL2.a or include/SDL2/SDL.h")
    endif()
    set(SDL2_INCLUDE_DIRS "${SDL2_ROOT}/include")
    set(SDL2_LIBRARIES "${SDL2_ROOT}/lib/libSDL2.a")
  else()
    add_compile_options(-sUSE_SDL=2)
    add_link_options(-sUSE_SDL=2)
    set(SDL2_INCLUDE_DIRS "")
    set(SDL2_LIBRARIES "")
  endif()
  set(BUILD_TESTING OFF CACHE BOOL "No tests in the WebAssembly build" FORCE)
elseif(NOT SDL2_LIBRARIES)
  # (Unless given: a cross build whose SDL2 CMake files name the target's
  # own paths passes SDL2_INCLUDE_DIRS and SDL2_LIBRARIES, see nix/r36s.nix.)
  find_package(SDL2 REQUIRED)
endif()

# OpenGL ES 2.0 present path (Android, Emscripten, R36S / ArkOS Mali).
# Desktop defaults to GL 2.x compatibility via SDL_opengl.h.
option(ENABLE_OPENGLES2 "Use OpenGL ES 2.0 (shaders + ES context profile)" OFF)
if(ENABLE_OPENGLES2)
  message(STATUS "ENABLE_OPENGLES2: GLES2 present path")
endif()

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
  src/amiga/savestate.cpp
  src/amiga/ser.hpp
  src/amiga/hle.cpp
  src/amiga/hle.hpp
  src/amiga/hunk.hpp
  src/amiga/adf.hpp
  src/recomp/recomp.hpp
  src/recomp/runtime.hpp
)
target_link_libraries(amiga PUBLIC m68k)

# Static recompilation of the game code (tools/recomp.py) at build time.
# Optional: needs the original executable once, either as FA18_EXE or
# extracted from FA18_ADF.  The ADF is a *runtime* argument to the binary
# and is never required to configure or compile; without the executable
# we fall back to the 68000 interpreter (FA18_RECOMP=OFF).
# Cross / WASM / Android builds default to OFF (no host Python recomp step
# of a 68000 binary on the target toolchain).
if(EMSCRIPTEN OR ANDROID OR CMAKE_CROSSCOMPILING)
  option(FA18_RECOMP "Build with statically recompiled game code" OFF)
else()
  option(FA18_RECOMP "Build with statically recompiled game code" ON)
endif()
set(FA18_EXE "${CMAKE_SOURCE_DIR}/assets/F-18 Interceptor" CACHE FILEPATH
    "Path to the unpacked 'F-18 Interceptor' executable (build-time only)")
set(FA18_ADF "" CACHE FILEPATH
    "Optional ADF used only to extract the executable for recomp at build time")
set(RECOMP_EXE "${FA18_EXE}")
set(RECOMP_EXTRA_DEPS "")
if(FA18_RECOMP AND NOT EXISTS "${RECOMP_EXE}")
  if(NOT FA18_ADF STREQUAL "" AND EXISTS "${FA18_ADF}")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(RECOMP_EXE "${CMAKE_BINARY_DIR}/F-18 Interceptor")
    add_custom_command(
      OUTPUT ${RECOMP_EXE}
      COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/adf.py extract-one
              "${FA18_ADF}" "F-18 Interceptor" ${RECOMP_EXE}
      DEPENDS "${FA18_ADF}" ${CMAKE_SOURCE_DIR}/tools/adf.py
      COMMENT "Extracting F-18 Interceptor from ADF for recomp"
      VERBATIM)
    set(RECOMP_EXTRA_DEPS ${RECOMP_EXE})
  else()
    message(STATUS
      "FA18_RECOMP requested but no executable found (FA18_EXE=${FA18_EXE}). "
      "Building interpreter-only; pass -DFA18_EXE=... or -DFA18_ADF=... to enable recomp.")
    set(FA18_RECOMP OFF)
  endif()
endif()
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
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/recomp.py ${RECOMP_EXE} ${RECOMP_DIR}
            --seeds ${CMAKE_SOURCE_DIR}/tools/recomp_seeds.txt
    DEPENDS ${RECOMP_EXE} ${RECOMP_EXTRA_DEPS}
            ${CMAKE_SOURCE_DIR}/tools/recomp.py ${CMAKE_SOURCE_DIR}/tools/loadseg.py
            ${CMAKE_SOURCE_DIR}/tools/hunks.py ${CMAKE_SOURCE_DIR}/tools/recomp_seeds.txt
    COMMENT "Recompiling 68000 game code to C++"
    VERBATIM)
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
target_include_directories(native PUBLIC src ${SDL2_INCLUDE_DIRS})
target_link_libraries(native PUBLIC amiga)
if(ENABLE_OPENGLES2)
  target_compile_definitions(native PUBLIC FA18_OPENGLES2=1)
  # So main() prefers an ES context when the port was built for GLES.
  add_compile_definitions(FA18_OPENGLES2=1)
endif()

# Line coverage of the native translation (tests/coverage_native.sh):
#   cmake -B build-cov -DFA18_COVERAGE=ON
option(FA18_COVERAGE "Instrument src/native for gcov" OFF)
if(FA18_COVERAGE)
  target_compile_options(native PRIVATE --coverage -O0)
endif()

add_executable(fa18 src/main.cpp src/stats_overlay.cpp $<TARGET_OBJECTS:native>)
if(FA18_COVERAGE)
  target_link_options(fa18 PRIVATE --coverage)
endif()

if(EMSCRIPTEN)
  target_include_directories(fa18 PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(fa18 PRIVATE amiga ${SDL2_LIBRARIES})
elseif(SDL2_LIBRARIES)
  target_include_directories(fa18 PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(fa18 PRIVATE amiga ${SDL2_LIBRARIES})
else()
  target_link_libraries(fa18 PRIVATE amiga SDL2::SDL2)
endif()

# Desktop GL: link libGL so SDL_GL_GetProcAddress can resolve symbols.
# GLES2 targets (Emscripten, Android, R36S) get entry points from the EGL
# driver via SDL — do not pull desktop OpenGL::GL.
if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT ENABLE_OPENGLES2)
  find_package(OpenGL)
  if(OpenGL_FOUND)
    target_link_libraries(fa18 PRIVATE OpenGL::GL)
  endif()
endif()

target_compile_definitions(fa18 PRIVATE
  FA18_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/fa18-interceptor")

if(WIN32)
  # Windowed program (no console) on Windows; MinGW runtime linked static.
  target_compile_definitions(fa18 PRIVATE _WIN32_WINNT=0x0601)
  set_target_properties(fa18 PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MINGW)
    target_link_options(fa18 PRIVATE -static-libgcc -static-libstdc++)
  endif()
endif()

if(EMSCRIPTEN)
  # The web page: fa18.html (also as index.html) with its .js and .wasm.
  # ADF is NOT embedded (not redistributable); the shell offers an Upload
  # button that stores the image in IDBFS and boots from there.
  set(FA18_SHELL "${CMAKE_SOURCE_DIR}/mk/wasm/shell.html")
  target_link_options(fa18 PRIVATE
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:-sMAXIMUM_MEMORY=512MB"
    "SHELL:-sFORCE_FILESYSTEM=1"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap','UTF8ToString']"
    "SHELL:-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
    "SHELL:-sASSERTIONS=0"
    "SHELL:-sNO_EXIT_RUNTIME=1"
    "SHELL:--shell-file=${FA18_SHELL}"
    "SHELL:-lidbfs.js"
  )
  set_target_properties(fa18 PROPERTIES OUTPUT_NAME "fa18" SUFFIX ".html")
  # Install the site at the prefix root so `nix run` can serve it.
  install(FILES
    $<TARGET_FILE_DIR:fa18>/fa18.html
    $<TARGET_FILE_DIR:fa18>/fa18.js
    $<TARGET_FILE_DIR:fa18>/fa18.wasm
    DESTINATION .
    OPTIONAL)
  install(FILES $<TARGET_FILE_DIR:fa18>/fa18.html
    DESTINATION . RENAME index.html OPTIONAL)
else()
  # CPU conformance test against the SingleStepTests 68000 suite:
  #   ./build/cpu_sst path/to/m68000/v1/*.json.bin
  if(NOT ANDROID AND NOT CMAKE_CROSSCOMPILING)
    add_executable(cpu_sst tests/cpu_sst.cpp)
    target_link_libraries(cpu_sst PRIVATE m68k)
  endif()
  install(TARGETS fa18 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
# Game data (the original ADF) is not redistributable and is not installed.
