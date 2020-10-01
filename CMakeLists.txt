cmake_minimum_required(VERSION 3.15)

project(libvarlink++)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(VarlinkWrapper)

# INTERFACE target for main header library
varlink_wrapper(include/varlink/org.varlink.service.varlink)
add_library(varlink++ OBJECT include/varlink/org.varlink.service.varlink.hpp)
target_include_directories(varlink++ PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}/3rdparty/pegtl/include
        )

#add_subdirectory(tool)

include(CTest)

add_subdirectory(3rdparty/googletest EXCLUDE_FROM_ALL)
add_subdirectory(test)

# TEST DEV STUFF
varlink_wrapper(org.example.more.varlink)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(testdev test.cpp org.example.more.varlink.hpp)
target_link_libraries(testdev varlink++ pthread)
# END TEST DEV STUFF

