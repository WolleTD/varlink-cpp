cmake_minimum_required(VERSION 3.15)

project(libvarlink++)

option(VARLINK_USE_EXTERNAL_JSON "Use external nlohmann/json.hpp" OFF)
option(VARLINK_USE_EXTERNAL_PEGTL "Use external taocpp/pegtl" OFF)
option(VARLINK_BUILD_TESTS "Build tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(VarlinkWrapper)

add_subdirectory(3rdparty EXCLUDE_FROM_ALL)

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

if (VARLINK_BUILD_TESTS)
    include(CTest)
    add_subdirectory(test)
endif()
