cmake_minimum_required(VERSION 3.16)

project(network-framework
    VERSION 0.1.0
    DESCRIPTION "Declarative HTTP client framework for Qt"
    LANGUAGES CXX)

option(NF_BUILD_TESTS "Build unit tests" ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core)
if(NF_BUILD_TESTS)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Test)
endif()

add_subdirectory(third_party/qtpromise)

set(CMAKE_AUTOMOC ON)

add_library(nf
    src/version.cpp)
add_library(nf::nf ALIAS nf)

target_include_directories(nf PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(nf PUBLIC cxx_std_17)
target_link_libraries(nf PUBLIC Qt5::Core qtpromise::qtpromise)

if(NF_BUILD_TESTS)
    enable_testing()
    add_executable(tst_smoke tests/tst_smoke.cpp)
    target_link_libraries(tst_smoke PRIVATE nf::nf Qt5::Test)
    add_test(NAME tst_smoke COMMAND tst_smoke)
endif()
