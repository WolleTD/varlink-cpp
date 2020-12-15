cmake_minimum_required(VERSION 3.15)

project(libvarlink++)

include(CMakeDependentOption)

option(VARLINK_NO_DOWNLOADS "Don't use CPM, provide all dependencies externally" OFF)
option(VARLINK_USE_BOOST "Use boost/asio instead of standalone" OFF)
option(VARLINK_BUILD_TESTS "Build tests" ON)
cmake_dependent_option(VARLINK_USE_EXTERNAL_JSON "Use external nlohmann/json.hpp" OFF "NOT VARLINK_NO_DOWNLOADS" ON)
cmake_dependent_option(VARLINK_USE_EXTERNAL_PEGTL "Use external taocpp/pegtl" OFF "NOT VARLINK_NO_DOWNLOADS" ON)
cmake_dependent_option(VARLINK_USE_EXTERNAL_CATCH2 "Use external catch2" OFF "NOT VARLINK_NO_DOWNLOADS" ON)
cmake_dependent_option(VARLINK_USE_EXTERNAL_ASIO "Use external asio" OFF "NOT VARLINK_NO_DOWNLOADS;NOT VARLINK_USE_BOOST" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(VarlinkWrapper)
include(CPM)

if(NOT VARLINK_USE_EXTERNAL_JSON)
    CPMAddPackage(
        NAME nlohmann_json
        URL http://github.com/nlohmann/json/releases/download/v3.9.1/include.zip
        URL_HASH SHA256=6bea5877b1541d353bd77bdfbdb2696333ae5ed8f9e8cc22df657192218cad91
        VERSION 3.9.1
    )
    if (nlohmann_json_ADDED)
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
        target_include_directories(nlohmann_json::nlohmann_json INTERFACE ${nlohmann_json_SOURCE_DIR}/include)
    endif()
else()
    if (NOT TARGET nlohmann_json::nlohmann_json)
        find_package(nlohmann_json REQUIRED)
    endif()
endif()

if(NOT VARLINK_USE_EXTERNAL_PEGTL)
    CPMAddPackage(
            NAME pegtl
            GITHUB_REPOSITORY taocpp/pegtl
            GIT_TAG 2.8.3
            OPTIONS
            "PEGTL_BUILD_TESTS OFF"
            "PEGTL_BUILD_EXAMPLES OFF"
    )
else()
    find_package(pegtl REQUIRED)
endif()

if(NOT TARGET asio)
    add_library(asio INTERFACE IMPORTED)
    if(VARLINK_USE_BOOST)
        find_package(Boost REQUIRED)
        target_link_libraries(asio INTERFACE Boost::headers)
    elseif(NOT VARLINK_USE_EXTERNAL_ASIO)
        CPMAddPackage(
                NAME asio
                URL https://github.com/chriskohlhoff/asio/archive/asio-1-18-0.zip
                VERSION 1.18.0
        )
        if(asio_ADDED)
            target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
        endif()
    endif()
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
    target_link_libraries(asio INTERFACE pthread stdc++fs)
endif()


# main header only library

varlink_wrapper(include/varlink/detail/org.varlink.service.varlink)

add_library(varlink++ OBJECT include/varlink/detail/org.varlink.service.varlink.hpp)

target_include_directories(varlink++ PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_BINARY_DIR}/include
        )

target_link_libraries(varlink++ INTERFACE nlohmann_json::nlohmann_json taocpp::pegtl asio)

# command line tool and more example

#add_subdirectory(tool)
add_subdirectory(example)

# testing

if(VARLINK_BUILD_TESTS)
    if(NOT VARLINK_USE_EXTERNAL_CATCH2)
        CPMAddPackage(
                NAME Catch2
                GITHUB_REPOSITORY catchorg/Catch2
                VERSION 2.13.2
        )
    else()
        find_package(Catch2 REQUIRED)
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
