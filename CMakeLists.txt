cmake_minimum_required(VERSION 3.25)

project(libvarlink++ LANGUAGES CXX VERSION 0.9)

include(CMakeDependentOption)

option(VARLINK_NO_DOWNLOADS "Don't use CPM, provide all dependencies externally" OFF)
option(VARLINK_USE_BOOST "Use boost/asio instead of standalone" OFF)
option(VARLINK_DISABLE_CTRE "Use std::regex instead of ctre" OFF)
option(VARLINK_USE_STRINGS "Use std::string instead of std::string_view for interface and members" OFF)
option(VARLINK_BUILD_TESTS "Build tests" ON)
option(VARLINK_BUILD_EXAMPLES "Build examples" OFF)
cmake_dependent_option(VARLINK_USE_EXTERNAL_JSON "Use external nlohmann/json.hpp" OFF "NOT VARLINK_NO_DOWNLOADS" ON)
cmake_dependent_option(VARLINK_USE_EXTERNAL_CATCH2 "Use external catch2" OFF "NOT VARLINK_NO_DOWNLOADS" ON)
cmake_dependent_option(VARLINK_USE_EXTERNAL_ASIO "Use external asio" OFF "NOT VARLINK_NO_DOWNLOADS;NOT VARLINK_USE_BOOST" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(VarlinkWrapper)
include(CPM)

if (NOT VARLINK_USE_EXTERNAL_JSON)
    CPMAddPackage(
            NAME nlohmann_json
            URL http://github.com/nlohmann/json/releases/download/v3.11.3/include.zip
            URL_HASH SHA256=a22461d13119ac5c78f205d3df1db13403e58ce1bb1794edc9313677313f4a9d
            VERSION 3.11.3
    )
    if (nlohmann_json_ADDED)
        add_library(nlohmann_json INTERFACE IMPORTED)
        target_include_directories(nlohmann_json INTERFACE ${nlohmann_json_SOURCE_DIR}/include)
    endif ()
else ()
    if (NOT TARGET nlohmann_json)
        find_package(nlohmann_json REQUIRED)
    endif ()
endif ()

if (NOT TARGET asio)
    add_library(asio INTERFACE IMPORTED)
    if (VARLINK_USE_BOOST)
        find_package(Boost REQUIRED)
        target_link_libraries(asio INTERFACE Boost::headers)
    elseif (NOT VARLINK_USE_EXTERNAL_ASIO)
        CPMAddPackage(
                NAME asio
                URL https://github.com/chriskohlhoff/asio/archive/asio-1-29-0.zip
                URL_HASH SHA256=45a7967e9bbfe8b7af404daeaed4b9214fd663bef00ad4f52c3c92399297445b
                VERSION 1.29.0
        )
        if (asio_ADDED)
            target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
        endif ()
    endif ()
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
    target_link_libraries(asio INTERFACE pthread)
endif ()

if (NOT VARLINK_DISABLE_CTRE)
    CPMAddPackage("https://github.com/hanickadot/compile-time-regular-expressions.git@3.8.1")
endif ()

# main library

varlink_wrapper(source/org.varlink.service.varlink)

add_library(varlink++
        source/interface.cpp
        source/member.cpp
        source/service.cpp
        source/org.varlink.service.varlink.hpp
)

target_include_directories(varlink++
        PUBLIC ${PROJECT_SOURCE_DIR}/include
        PRIVATE ${PROJECT_BINARY_DIR}/source
)

target_link_libraries(varlink++ PUBLIC nlohmann_json asio stdc++fs)

if (VARLINK_DISABLE_CTRE)
    target_compile_definitions(varlink++ PUBLIC VARLINK_DISABLE_CTRE)
else ()
    target_link_libraries(varlink++ PRIVATE ctre)
endif ()

if (VARLINK_USE_STRINGS)
    target_compile_definitions(varlink++ PUBLIC VARLINK_USE_STRINGS)
endif ()

# command line tool and more example

#add_subdirectory(tool)
if (VARLINK_BUILD_EXAMPLES)
    add_subdirectory(example)
endif ()

# testing

if (VARLINK_BUILD_TESTS)
    if (NOT VARLINK_USE_EXTERNAL_CATCH2)
        CPMAddPackage("gh:catchorg/Catch2@3.5.0")
    else ()
        find_package(Catch2 REQUIRED)
    endif ()
    include(CTest)
    add_subdirectory(tests)
endif ()
