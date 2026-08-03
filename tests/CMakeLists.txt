cmake_minimum_required(VERSION 3.13)

project(secd-machine-tests C)

# Find Google Test
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "Google Test not found, using simple tests")
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Test sources
set(TEST_SOURCES
    test_heap.c
    test_machine.c
)

# Core sources for linking
set(CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/heap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/gc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/machine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/bytecode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/primitives.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/symbols.c
)

# Build tests
foreach(TEST ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST} NAME_WE)
    add_executable(${TEST_NAME} ${TEST} ${CORE_SOURCES})
    target_link_libraries(${TEST_NAME} m)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()
