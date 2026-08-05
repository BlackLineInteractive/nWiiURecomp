add_library(nwiiu_recomp STATIC
    src/native_generator.cpp
    src/project_generator.cpp
    src/recompile_cli.cpp
    src/runner_cli.cpp
    src/shader_corpus.cpp
    src/shader_gfd.cpp
    src/shader_identity.cpp
    src/shader_sarc.cpp
    src/shader_sharcfb.cpp
    src/shader_walk.cpp
    src/shader_yaz0.cpp)
set_target_properties(nwiiu_recomp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(nwiiu_recomp PUBLIC include)
# OpenSSL is reachable transitively via nwiiu_analyzer, but shader_identity.cpp
# calls EVP directly, so name it as a direct dependency.
target_link_libraries(nwiiu_recomp PUBLIC nwiiu_runtime OpenSSL::Crypto)
target_compile_options(nwiiu_recomp PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

add_executable(nwiiu-run src/main.cpp)
target_link_libraries(nwiiu-run PRIVATE nwiiu_recomp)
target_compile_options(nwiiu-run PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

add_executable(nwiiu-recompile src/recompile_main.cpp)
target_link_libraries(nwiiu-recompile PRIVATE nwiiu_recomp)
target_compile_options(nwiiu-recompile PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

add_executable(native_test tests/native_test.cpp tests/generated/native_fixture.cpp)
target_link_libraries(native_test PRIVATE nwiiu_recomp)
target_include_directories(native_test PRIVATE ../nWiiUAnalyzer/tests)
target_compile_definitions(native_test PRIVATE
    NWIIU_NATIVE_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/native_fixture.cpp")
target_compile_options(native_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
add_test(NAME native_test COMMAND native_test)

add_executable(runner_cli_test tests/runner_cli_test.cpp)
target_link_libraries(runner_cli_test PRIVATE nwiiu_recomp)
target_include_directories(runner_cli_test PRIVATE ../nWiiUAnalyzer/tests)
target_compile_options(runner_cli_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
add_test(NAME runner_cli_test COMMAND runner_cli_test)

add_executable(project_generator_test tests/project_generator_test.cpp)
target_link_libraries(project_generator_test PRIVATE nwiiu_recomp)
target_include_directories(project_generator_test PRIVATE ../nWiiUAnalyzer/tests)
target_compile_options(project_generator_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
add_test(NAME project_generator_test COMMAND project_generator_test)

add_executable(recompile_cli_test tests/recompile_cli_test.cpp)
target_link_libraries(recompile_cli_test PRIVATE nwiiu_recomp)
target_include_directories(recompile_cli_test PRIVATE ../nWiiUAnalyzer/tests)
target_compile_definitions(recompile_cli_test PRIVATE
    NWIIU_CMAKE_COMMAND="${CMAKE_COMMAND}"
    NWIIU_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(recompile_cli_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
add_test(NAME recompile_cli_test COMMAND recompile_cli_test)

# Shader AOT tests. Helper defined once; every later shader test is one line.
# Mirrors add_nwiiu_test in nWiiUAnalyzer/CMakeLists.txt:20-26.
function(add_shader_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE nwiiu_recomp)
    target_include_directories(${name} PRIVATE ../nWiiUAnalyzer/tests)
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_shader_test(shader_yaz0_test tests/shader_yaz0_test.cpp)
add_shader_test(shader_sarc_test tests/shader_sarc_test.cpp)
add_shader_test(shader_gfd_test tests/shader_gfd_test.cpp)
add_shader_test(shader_sharcfb_test tests/shader_sharcfb_test.cpp)
add_shader_test(shader_walk_test tests/shader_walk_test.cpp)
add_shader_test(shader_identity_test tests/shader_identity_test.cpp)
add_shader_test(shader_corpus_test tests/shader_corpus_test.cpp)
add_shader_test(shader_corpus_gate_test tests/shader_corpus_gate_test.cpp)
