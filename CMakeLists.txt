cmake_minimum_required(VERSION 3.16)
project(xlpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "build type" FORCE)
endif()

# --- xlnt (submodule; requires `git submodule update --init --recursive`) ----
set(STATIC ON CACHE BOOL "build xlnt as a static library")
set(TESTS OFF CACHE BOOL "xlnt tests off")
set(BENCHMARKS OFF CACHE BOOL "xlnt benchmarks off")
set(SAMPLES OFF CACHE BOOL "xlnt samples off")
add_subdirectory(third_party/xlnt)

# --- xlpp core ----------------------------------------------------------------
# sidecar: direct parse of xl/workbook.xml for the pieces xlnt drops
# (defined-name values/scope and calcPr iteration settings). Uses the miniz
# already compiled into libxlnt.a for zip access.
add_library(xlpp_core STATIC
    src/ast.cpp
    src/lexer.cpp
    src/parser.cpp
    src/sidecar.cpp
)
target_include_directories(xlpp_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xlnt/third-party/miniz
)
target_link_libraries(xlpp_core PUBLIC xlnt)
target_compile_options(xlpp_core PRIVATE -Wall -Wextra)

# --- tools ---------------------------------------------------------------------
add_executable(xlpp_probe tools/probe.cpp)
target_link_libraries(xlpp_probe PRIVATE xlpp_core)
target_compile_options(xlpp_probe PRIVATE -Wall -Wextra)

add_executable(parse_corpus tools/parse_corpus.cpp)
target_link_libraries(parse_corpus PRIVATE xlpp_core)
target_compile_options(parse_corpus PRIVATE -Wall -Wextra)
