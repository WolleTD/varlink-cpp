cmake_minimum_required(VERSION 3.15)

project(libvarlink++)

option(VARLINK_USE_EXTERNAL_JSON "Use external nlohmann/json.hpp" OFF)
option(VARLINK_USE_EXTERNAL_PEGTL "Use external taocpp/pegtl" OFF)
option(VARLINK_USE_EXTERNAL_CATCH2 "Use external catch2" OFF)
option(VARLINK_BUILD_TESTS "Build tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(VarlinkWrapper)
include(CPM)

if(NOT VARLINK_USE_EXTERNAL_JSON)
    CPMAddPackage(
        NAME nlohmann_json
        GITHUB_REPOSITORY ArthurSonzogni/nlohmann_json_cmake_fetchcontent
        VERSION 3.9.1
    )
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
endif()

if(NOT VARLINK_USE_EXTERNAL_ASIO)
    CPMAddPackage(
        NAME asio
        URL https://github.com/chriskohlhoff/asio/archive/asio-1-18-0.zip
        VERSION 1.18.0
    )
    if(asio_ADDED)
        add_library(asio INTERFACE)
        target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
        target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
        target_link_libraries(asio INTERFACE pthread)
    endif()
endif()


# main header only library

varlink_wrapper(include/varlink/org.varlink.service.varlink)

add_library(varlink++ OBJECT include/varlink/org.varlink.service.varlink.hpp)

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
    CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        VERSION 2.13.2
    )
    include(CTest)
    add_subdirectory(test)
endif()
