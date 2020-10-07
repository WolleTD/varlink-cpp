cmake_minimum_required(VERSION 3.15)

project(libvarlink++)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(VarlinkWrapper)

# main header only library

varlink_wrapper(include/varlink/org.varlink.service.varlink)

add_library(varlink++ OBJECT include/varlink/org.varlink.service.varlink.hpp)

target_include_directories(varlink++ PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}/3rdparty/pegtl/include
        )

target_link_libraries(varlink++ INTERFACE pthread)

# command line tool and more example

#add_subdirectory(tool)
add_subdirectory(example)

# testing

include(CTest)
add_subdirectory(3rdparty/googletest EXCLUDE_FROM_ALL)
add_subdirectory(test)
