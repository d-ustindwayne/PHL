cmake_minimum_required(VERSION 3.15)
project(phl-cpp VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    option(PHL_CPP_BUILD_TESTS "Build the tests" ON)
else()
    option(PHL_CPP_BUILD_TESTS "Build the tests" OFF)
endif()

add_library(phl_cpp INTERFACE)
add_library(phl::cpp ALIAS phl_cpp)
target_compile_features(phl_cpp INTERFACE cxx_std_17)
target_include_directories(phl_cpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

include(GNUInstallDirs)
install(TARGETS phl_cpp EXPORT phlCppTargets)
install(FILES include/phl.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT phlCppTargets NAMESPACE phl:: FILE phl-cppConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phl-cpp)

if(PHL_CPP_BUILD_TESTS)
    enable_testing()
    add_executable(test_phl tests/test_phl.cpp)
    target_link_libraries(test_phl PRIVATE phl_cpp)
    set_target_properties(test_phl PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(test_phl PRIVATE /W4 /utf-8)
    else()
        target_compile_options(test_phl PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()
    add_test(NAME conformance COMMAND test_phl ${CMAKE_CURRENT_SOURCE_DIR}/../../conformance)
endif()
