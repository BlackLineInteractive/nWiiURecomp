add_library(nwiiu_analyzer STATIC
    src/analysis.cpp
    src/game_config.cpp
    src/hash.cpp
    src/manifest.cpp
    src/rpx.cpp
)
set_target_properties(nwiiu_analyzer PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(nwiiu_analyzer PUBLIC include)
target_link_libraries(nwiiu_analyzer PUBLIC OpenSSL::Crypto ZLIB::ZLIB)

add_executable(nwiiu-analyze src/main.cpp)
target_link_libraries(nwiiu-analyze PRIVATE nwiiu_analyzer)

target_compile_options(nwiiu_analyzer PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
)

function(add_nwiiu_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE nwiiu_analyzer)
    target_include_directories(${name} PRIVATE tests)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_nwiiu_test(game_config_test tests/game_config_test.cpp)
add_nwiiu_test(hash_test tests/hash_test.cpp)
add_nwiiu_test(rpx_test tests/rpx_test.cpp)
add_nwiiu_test(analysis_test tests/analysis_test.cpp)
add_nwiiu_test(manifest_test tests/manifest_test.cpp)
target_compile_definitions(manifest_test PRIVATE
    NWIIU_ANALYZER_SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/schema/analysis-manifest.schema.json"
)
