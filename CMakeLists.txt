cmake_minimum_required(VERSION 3.20)
project(nWiiURecomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)

enable_testing()
add_subdirectory(nWiiUAnalyzer)
add_subdirectory(nWiiURuntime)
add_subdirectory(nWiiURecomp)

# Studio clones raylib, ImGui and four more projects at configure time, so a
# build of the CLIs would otherwise need the network — and recompile_cli_test,
# which configures this tree from scratch, would pay for it on every run.
# It is also the only component that does not build today: its sources came
# from NWiiRecomp and still include that project's headers.
option(NWIIU_BUILD_STUDIO "Build the nWiiUStudio GUI (needs network at configure time)" OFF)
if(NWIIU_BUILD_STUDIO)
    add_subdirectory(nWiiUStudio)
endif()

if(DEFINED NWIIU_GENERATED_PROGRAM
   AND NOT NWIIU_GENERATED_PROGRAM STREQUAL "")
    if(NOT IS_ABSOLUTE "${NWIIU_GENERATED_PROGRAM}")
        message(FATAL_ERROR
            "NWIIU_GENERATED_PROGRAM must be an absolute path")
    endif()
    set(NWIIU_GENERATED_PROGRAM_CMAKE
        "${NWIIU_GENERATED_PROGRAM}/program.cmake")
    if(NOT EXISTS "${NWIIU_GENERATED_PROGRAM_CMAKE}")
        message(FATAL_ERROR
            "NWIIU_GENERATED_PROGRAM does not contain program.cmake: "
            "${NWIIU_GENERATED_PROGRAM}")
    endif()
    include("${NWIIU_GENERATED_PROGRAM_CMAKE}")
    # The generated targets are named after the profile's target_prefix, so the
    # name to check for comes from the caller. It defaults to wwhd, which is
    # what tools/build-wwhd-port.sh and the Cemu patch expect.
    if(NOT DEFINED NWIIU_GENERATED_PREFIX OR NWIIU_GENERATED_PREFIX STREQUAL "")
        set(NWIIU_GENERATED_PREFIX wwhd)
    endif()
    if(NOT TARGET ${NWIIU_GENERATED_PREFIX}-native)
        message(FATAL_ERROR
            "generated program.cmake must define target "
            "${NWIIU_GENERATED_PREFIX}-native")
    endif()
endif()
