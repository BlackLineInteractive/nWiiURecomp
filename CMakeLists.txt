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
add_subdirectory(nWiiUStudio)

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
    if(NOT TARGET wwhd-native)
        message(FATAL_ERROR
            "generated program.cmake must define target wwhd-native")
    endif()
endif()
