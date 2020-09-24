cmake_minimum_required(VERSION 3.15)

project(libvarlink++)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(varlink_wrapper VARLINK_INTERFACE)
    add_custom_command(OUTPUT "${VARLINK_INTERFACE}.hpp"
            COMMAND "cmake" ARGS "-P"
            "${CMAKE_SOURCE_DIR}/varlink-wrapper.cmake"
            "${CMAKE_CURRENT_SOURCE_DIR}/${VARLINK_INTERFACE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${VARLINK_INTERFACE}.hpp"
            DEPENDS "${VARLINK_INTERFACE}"
            )
endfunction()

# INTERFACE target for main header library
varlink_wrapper(include/varlink/org.varlink.service.varlink)
add_library(varlink++ OBJECT include/varlink/org.varlink.service.varlink.hpp)
target_include_directories(varlink++ PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}/pegtl/include
        )

add_compile_definitions(_GNU_SOURCE __SANE_USERSPACE_TYPES__)
add_compile_options(-Wall -Wextra -fvisibility=hidden)
add_compile_options(-g -fprofile-arcs -ftest-coverage)
link_libraries(gcov)

#add_subdirectory(tool)
include(CTest)
add_subdirectory(test)

# TEST DEV STUFF
varlink_wrapper(org.example.more.varlink)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(testdev test.cpp org.example.more.varlink.hpp)
target_link_libraries(testdev varlink++ pthread)
# END TEST DEV STUFF

