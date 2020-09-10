add_subdirectory(googletest EXCLUDE_FROM_ALL)

add_executable(gtest_units interface.cpp client.cpp service.cpp message.cpp)
target_link_libraries(gtest_units PRIVATE gtest gmock gtest_main varlink++)
add_test(unittests gtest_units)
