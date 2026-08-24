cmake_minimum_required(VERSION 3.16)

project(network-framework
    VERSION 0.1.0
    DESCRIPTION "Declarative HTTP client framework for Qt"
    LANGUAGES CXX)

option(NF_BUILD_TESTS "Build unit tests" ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Network)
if(NF_BUILD_TESTS)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Test)
endif()

add_subdirectory(third_party/qtpromise)

set(CMAKE_AUTOMOC ON)

add_library(NetworkLib
    include/client/NetworkClientError.h
    include/client/NetworkException.h
    include/serializer/JsonSerializer.h
    src/client/NetworkClientError.cpp
    src/client/NetworkException.cpp
    src/core/Version.cpp
    src/serializer/SerializerException.cpp
    src/serializer/JsonSerializer.cpp)
add_library(NetCore::NetworkLib ALIAS NetworkLib)

target_include_directories(NetworkLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(NetworkLib PUBLIC cxx_std_17)
target_compile_definitions(NetworkLib PUBLIC NETWORK_STATIC)
target_link_libraries(NetworkLib PUBLIC Qt5::Core Qt5::Network qtpromise::qtpromise)

if(MSVC)
    target_compile_options(NetworkLib PUBLIC /utf-8)
endif()

add_subdirectory(examples)

if(NF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
