add_subdirectory(googletest EXCLUDE_FROM_ALL)

function(vltest NAME)
    add_executable(test_${NAME} ${ARGN})
    target_link_libraries(test_${NAME} PRIVATE gtest gmock gtest_main varlink++)
    add_test(${NAME} test_${NAME})
endfunction()

vltest(unittests client.cpp interface.cpp message.cpp service.cpp)
