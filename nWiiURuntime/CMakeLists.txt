find_package(PkgConfig REQUIRED)
pkg_check_modules(SHADERC REQUIRED IMPORTED_TARGET shaderc)

add_library(nwiiu_runtime STATIC src/memory.cpp src/execution_image.cpp
                                  src/ppc_semantics.cpp src/executor.cpp
                                  src/cafe_runtime.cpp src/cafe_proc_ui.cpp
                                  src/gx2_runtime.cpp
                                  src/latte_surface.cpp src/latte_shader.cpp
                                  src/cafe_coreinit.cpp
                                  src/cafe_filesystem.cpp src/cafe_olv.cpp
                                  src/cafe_padscore.cpp src/cafe_act.cpp
                                  src/machine.cpp src/native_hooks.cpp
                                  src/patch_guard.cpp)
set_target_properties(nwiiu_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(nwiiu_runtime PUBLIC include)
target_link_libraries(nwiiu_runtime PUBLIC nwiiu_analyzer PkgConfig::SHADERC)
target_compile_options(nwiiu_runtime PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

add_executable(memory_test tests/memory_test.cpp)
target_link_libraries(memory_test PRIVATE nwiiu_runtime)
target_include_directories(memory_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME memory_test COMMAND memory_test)

add_executable(patch_guard_test tests/patch_guard_test.cpp)
target_link_libraries(patch_guard_test PRIVATE nwiiu_runtime)
target_include_directories(patch_guard_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME patch_guard_test COMMAND patch_guard_test)

add_executable(paired_single_test tests/paired_single_test.cpp)
target_link_libraries(paired_single_test PRIVATE nwiiu_runtime)
target_include_directories(paired_single_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME paired_single_test COMMAND paired_single_test)

add_executable(reservation_test tests/reservation_test.cpp)
target_link_libraries(reservation_test PRIVATE nwiiu_runtime)
target_include_directories(reservation_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME reservation_test COMMAND reservation_test)

add_executable(execution_image_test tests/execution_image_test.cpp)
target_link_libraries(execution_image_test PRIVATE nwiiu_runtime)
target_include_directories(execution_image_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME execution_image_test COMMAND execution_image_test)

add_executable(executor_test tests/executor_test.cpp)
target_link_libraries(executor_test PRIVATE nwiiu_runtime)
target_include_directories(executor_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME executor_test COMMAND executor_test)

add_executable(cafe_runtime_test tests/cafe_runtime_test.cpp)
target_link_libraries(cafe_runtime_test PRIVATE nwiiu_runtime)
target_include_directories(cafe_runtime_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME cafe_runtime_test COMMAND cafe_runtime_test)

add_executable(cafe_proc_ui_test tests/cafe_proc_ui_test.cpp)
target_link_libraries(cafe_proc_ui_test PRIVATE nwiiu_runtime)
target_include_directories(cafe_proc_ui_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME cafe_proc_ui_test COMMAND cafe_proc_ui_test)

add_executable(gx2_runtime_test tests/gx2_runtime_test.cpp)
target_link_libraries(gx2_runtime_test PRIVATE nwiiu_runtime)
target_include_directories(gx2_runtime_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME gx2_runtime_test COMMAND gx2_runtime_test)

add_executable(latte_surface_test tests/latte_surface_test.cpp)
target_link_libraries(latte_surface_test PRIVATE nwiiu_runtime)
target_include_directories(latte_surface_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME latte_surface_test COMMAND latte_surface_test)

add_executable(latte_shader_test tests/latte_shader_test.cpp)
target_link_libraries(latte_shader_test PRIVATE nwiiu_runtime)
target_include_directories(latte_shader_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME latte_shader_test COMMAND latte_shader_test)

add_executable(cafe_coreinit_test tests/cafe_coreinit_test.cpp)
target_link_libraries(cafe_coreinit_test PRIVATE nwiiu_runtime)
target_include_directories(cafe_coreinit_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME cafe_coreinit_test COMMAND cafe_coreinit_test)

add_executable(cafe_heap_test tests/cafe_heap_test.cpp)
target_link_libraries(cafe_heap_test PRIVATE nwiiu_runtime)
target_include_directories(cafe_heap_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME cafe_heap_test COMMAND cafe_heap_test)

add_executable(machine_test tests/machine_test.cpp)
target_link_libraries(machine_test PRIVATE nwiiu_runtime)
target_include_directories(machine_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME machine_test COMMAND machine_test)

add_executable(cafe_filesystem_test tests/cafe_filesystem_test.cpp)
target_link_libraries(cafe_filesystem_test PRIVATE nwiiu_runtime)
target_include_directories(cafe_filesystem_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME cafe_filesystem_test COMMAND cafe_filesystem_test)

add_executable(cafe_olv_test tests/cafe_olv_test.cpp)
target_link_libraries(cafe_olv_test PRIVATE nwiiu_runtime)
target_include_directories(cafe_olv_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME cafe_olv_test COMMAND cafe_olv_test)

add_executable(boot_trace_test tests/boot_trace_test.cpp)
target_link_libraries(boot_trace_test PRIVATE nwiiu_runtime)
target_include_directories(boot_trace_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME boot_trace_test COMMAND boot_trace_test)

add_executable(cafe_padscore_test tests/cafe_padscore_test.cpp)
target_link_libraries(cafe_padscore_test PRIVATE nwiiu_runtime)
target_include_directories(cafe_padscore_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME cafe_padscore_test COMMAND cafe_padscore_test)

add_executable(cafe_act_test tests/cafe_act_test.cpp)
target_link_libraries(cafe_act_test PRIVATE nwiiu_runtime)
target_include_directories(cafe_act_test PRIVATE ../nWiiUAnalyzer/tests)
add_test(NAME cafe_act_test COMMAND cafe_act_test)
