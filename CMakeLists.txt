cmake_minimum_required(VERSION 3.15)

project(libvarlink++)

option(VARLINK_USE_EXTERNAL_JSON "Use external nlohmann/json.hpp" OFF)
option(VARLINK_USE_EXTERNAL_PEGTL "Use external taocpp/pegtl" OFF)
option(VARLINK_USE_EXTERNAL_GTEST "Use external googletest" OFF)
option(VARLINK_BUILD_TESTS "Build tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(VarlinkWrapper)
include(FindOrFetch)

FindPackage_or_FetchContent(nlohmann_json CACHE_VAR VARLINK_USE_EXTERNAL_JSON
        GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent
        GIT_TAG v3.9.1)

option(PEGTL_BUILD_TESTS OFF)
option(PEGTL_BUILD_EXAMPLES OFF)
FindPackage_or_FetchContent(pegtl CACHE_VAR VARLINK_USE_EXTERNAL_PEGTL
            GIT_REPOSITORY https://github.com/taocpp/pegtl
            GIT_TAG origin/master)

# main header only library

varlink_wrapper(include/varlink/org.varlink.service.varlink)

add_library(varlink++ OBJECT include/varlink/org.varlink.service.varlink.hpp)

target_include_directories(varlink++ PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_BINARY_DIR}/include
        )

target_link_libraries(varlink++ INTERFACE pthread nlohmann_json::nlohmann_json taocpp::pegtl)

# command line tool and more example

#add_subdirectory(tool)
add_subdirectory(example)

# testing

if(VARLINK_BUILD_TESTS)
    FindPackage_or_FetchContent(GTest CONFIG CACHE_VAR VARLINK_USE_EXTERNAL_GTEST
                GIT_REPOSITORY https://github.com/google/googletest
                GIT_TAG origin/master)
    if(NOT TARGET GTest::gtest)
        add_library(GTest::gtest ALIAS gtest)
        add_library(GTest::gmock ALIAS gmock)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
    include(CTest)
    add_subdirectory(test)
endif()
