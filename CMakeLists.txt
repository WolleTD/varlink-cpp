cmake_minimum_required(VERSION 3.15)

project(libvarlink++)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(varlink_wrapper VARLINK_INTERFACE)
    add_custom_command(OUTPUT "${VARLINK_INTERFACE}.cpp.inc"
            COMMAND "cmake" ARGS "-P"
            "${CMAKE_SOURCE_DIR}/varlink-wrapper.cmake"
            "${CMAKE_CURRENT_SOURCE_DIR}/${VARLINK_INTERFACE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${VARLINK_INTERFACE}.cpp.inc"
            DEPENDS "${VARLINK_INTERFACE}"
            )
endfunction()

add_compile_definitions(_GNU_SOURCE __SANE_USERSPACE_TYPES__)
add_compile_options(-Wall -Wextra -fvisibility=hidden)

add_subdirectory(lib)
#add_subdirectory(tool)
#add_subdirectory(test)

# TEST DEV STUFF
varlink_wrapper(org.example.more.varlink)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(test test.cpp org.example.more.varlink.cpp.inc)
target_link_libraries(test varlink++)
# END TEST DEV STUFF

