cmake_minimum_required(VERSION 3.12)
project(compile-prober)

include(CTest)

option(COMPILE_PROBER_BUILD_TEST "Build the compiler plugin test" ON)

if(NOT COMPILE_PROBER_GCC_PLUGIN_DIR)
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=plugin
        OUTPUT_VARIABLE COMPILE_PROBER_GCC_PLUGIN_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(NOT EXISTS ${COMPILE_PROBER_GCC_PLUGIN_DIR}/include/gcc-plugin.h)
    message(FATAL_ERROR "gcc-plugin.h not found under ${COMPILE_PROBER_GCC_PLUGIN_DIR}/include")
endif()

message("GCC plugin headers found in " ${COMPILE_PROBER_GCC_PLUGIN_DIR})

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCES
    src/plugin.cc 
    src/tracking.cc
    src/perf_output.cc
)

add_library(compile_prober SHARED ${SOURCES})
target_include_directories(compile_prober PRIVATE ${COMPILE_PROBER_GCC_PLUGIN_DIR}/include)



set_target_properties(compile_prober PROPERTIES CXX_STANDARD 20)
set_target_properties(compile_prober PROPERTIES COMPILE_FLAGS "-fno-rtti -g -Wall")
set_target_properties(compile_prober PROPERTIES PREFIX "" OUTPUT_NAME "compile-prober")

set(COMPILE_PROBER_PLUGIN_PATH ${CMAKE_BINARY_DIR}/compile-prober.so)
install(TARGETS compile_prober DESTINATION ${COMPILE_PROBER_GCC_PLUGIN_DIR})

if(COMPILE_PROBER_BUILD_TEST)
    enable_testing()
    add_subdirectory(test)
endif()
