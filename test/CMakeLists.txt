include(CodeCoverage)

add_compile_definitions(_GNU_SOURCE __SANE_USERSPACE_TYPES__)
add_compile_options(-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wpedantic -Wconversion -Wsign-conversion
        -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op
        -Wnull-dereference -Wuseless-cast -Wdouble-promotion -Wformat=2)
append_coverage_compiler_flags()

function(varlink_test NAME)
    add_executable(test_${NAME} ${ARGN})
    target_link_libraries(test_${NAME} PRIVATE gtest gmock varlink++)
    add_test(${NAME} test_${NAME})
endfunction()

varlink_test(unittests
        unit_client.cpp
        unit_interface.cpp
        unit_message.cpp
        unit_service.cpp
        unit_transport.cpp
        unit_uri.cpp
        unit_socket.cpp
        )
target_link_libraries(test_unittests PRIVATE gtest_main)

varlink_test(self_unix self_unix.cpp)
varlink_test(self_tcp self_tcp.cpp)
varlink_test(self_errors self_errors.cpp)

setup_target_for_coverage_gcovr_html(
    NAME coverage
    EXECUTABLE test_self_unix COMMAND test_self_tcp COMMAND test_self_errors COMMAND test_unittests
    EXCLUDE "${PROJECT_SOURCE_DIR}/3rdparty" "${PROJECT_SOURCE_DIR}/test")
