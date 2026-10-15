add_executable(compile_prober_fixture test.cc)
add_dependencies(compile_prober_fixture compile_prober)
target_compile_options(
    compile_prober_fixture
    PRIVATE
        "-fplugin=$<TARGET_FILE:compile_prober>"
        "-fplugin-arg-compile-prober-trace=${CMAKE_CURRENT_BINARY_DIR}/trace.json"
)

add_test(NAME compile_prober_fixture_runs COMMAND compile_prober_fixture)
add_test(
    NAME compile_prober_trace_format
    COMMAND ${CMAKE_COMMAND}
        -DTRACE_FILE=${CMAKE_CURRENT_BINARY_DIR}/trace.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_trace.cmake
)
set_tests_properties(compile_prober_trace_format
    PROPERTIES DEPENDS compile_prober_fixture_runs)
