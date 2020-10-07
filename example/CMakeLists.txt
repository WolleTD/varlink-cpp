varlink_wrapper(org.example.more.varlink)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(example_more more.cpp org.example.more.varlink.hpp)
target_link_libraries(example_more varlink++)
