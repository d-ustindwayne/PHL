cmake_minimum_required(VERSION 3.15)
project(phl VERSION 0.1.0 LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    option(PHL_BUILD_TESTS "Build the tests" ON)
else()
    option(PHL_BUILD_TESTS "Build the tests" OFF)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(PHL_SOURCES src/value.c src/parser.c src/writer.c)

# BUILD_SHARED_LIBS=ON gives a shared library instead of a static one.
add_library(phl ${PHL_SOURCES})
add_library(phl::phl ALIAS phl)
target_include_directories(phl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
set_target_properties(phl PROPERTIES
    PUBLIC_HEADER include/phl.h
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(phl PUBLIC PHL_SHARED PRIVATE PHL_BUILDING)
endif()

include(GNUInstallDirs)
install(TARGETS phl EXPORT phlTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT phlTargets NAMESPACE phl:: FILE phlConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phl)

if(PHL_BUILD_TESTS)
    enable_testing()

    if(MSVC)
        set(PHL_WARNINGS /W4)
    else()
        set(PHL_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()
    target_compile_options(phl PRIVATE ${PHL_WARNINGS})

    file(GLOB PHL_CASES
        ${CMAKE_CURRENT_SOURCE_DIR}/../../conformance/*/*/*.phl
        ${CMAKE_CURRENT_SOURCE_DIR}/../../conformance/*/*/*.hpl)

    # The tests use the library as a static one, whatever was asked for above.
    add_executable(test_phl tests/test_phl.c ${PHL_SOURCES})
    target_include_directories(test_phl PRIVATE include)
    add_test(NAME conformance COMMAND test_phl ${PHL_CASES})

    # Replacing the allocator relies on a forced include, which MSVC spells
    # differently, so it has its own flag there.
    add_executable(test_robust tests/test_robust.c ${PHL_SOURCES})
    target_include_directories(test_robust PRIVATE include src)
    target_compile_definitions(test_robust PRIVATE
        "PHL_MALLOC=test_malloc" "PHL_REALLOC=test_realloc" "PHL_FREE=test_free")
    if(MSVC)
        target_compile_options(test_robust PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/tests/alloc_hooks.h)
    else()
        target_compile_options(test_robust PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/tests/alloc_hooks.h)
    endif()
    add_test(NAME robustness COMMAND test_robust ${PHL_CASES})
endif()
