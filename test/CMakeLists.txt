add_compile_definitions(_GNU_SOURCE __SANE_USERSPACE_TYPES__)
add_compile_options(-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wpedantic -Wconversion -Wsign-conversion
        -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op
        -Wnull-dereference -Wdouble-promotion -Wformat=2)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
include(CodeCoverage)
append_coverage_compiler_flags()
endif ()

function(varlink_test NAME)
    add_executable(test_${NAME} ${ARGN})
    target_link_libraries(test_${NAME} PRIVATE GTest::gtest GTest::gmock varlink++)
    add_test(${NAME} test_${NAME})
endfunction()

function(bash_test NAME SCRIPT)
    configure_file(${SCRIPT} ${SCRIPT})
    add_test(NAME ${NAME} COMMAND ${SCRIPT})
endfunction()

varlink_test(unittests
        unit_interface.cpp
        unit_message.cpp
        unit_service.cpp
        unit_transport.cpp
        unit_uri.cpp
        unit_socket.cpp
        )
target_link_libraries(test_unittests PRIVATE GTest::gtest_main)

varlink_test(self_unix self_unix.cpp)
varlink_test(self_tcp self_tcp.cpp)
varlink_test(self_errors self_errors.cpp)

add_executable(cert_client cert_client.cpp)
target_link_libraries(cert_client PRIVATE varlink++)

varlink_wrapper(org.varlink.certification.varlink)
add_executable(cert_server cert_server.cpp org.varlink.certification.varlink.hpp)
target_include_directories(cert_server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(cert_server PRIVATE varlink++)

bash_test(certification self_cert.sh)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    setup_target_for_coverage_gcovr_html(
        NAME coverage
        EXECUTABLE test_self_unix COMMAND test_self_tcp COMMAND test_self_errors COMMAND test_unittests
        EXCLUDE "${PROJECT_SOURCE_DIR}/3rdparty" "${PROJECT_SOURCE_DIR}/test")
endif()