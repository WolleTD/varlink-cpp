add_subdirectory(googletest EXCLUDE_FROM_ALL)
include(CTest)

link_libraries(gtest gtest_main varlink++)

add_executable(test_interface interface.cpp)
add_test(interface test_interface)
