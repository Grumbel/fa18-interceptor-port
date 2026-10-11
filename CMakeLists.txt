cmake_minimum_required(VERSION 3.16)
project(fa18-interceptor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

add_executable(fa18
  src/main.cpp
  src/amiga_hunk.cpp
  src/amiga_hunk.hpp
)

target_include_directories(fa18 PRIVATE ${SDL2_INCLUDE_DIRS} src)
target_link_libraries(fa18 PRIVATE ${SDL2_LIBRARIES})

# Install assets next to binary for convenience during dev
add_custom_command(TARGET fa18 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:fa18>/assets)
