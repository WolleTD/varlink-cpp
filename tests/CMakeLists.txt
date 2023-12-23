add_compile_definitions(_GNU_SOURCE __SANE_USERSPACE_TYPES__)
add_compile_options(-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wpedantic -Wconversion -Wsign-conversion
        -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op
        -Wnull-dereference -Wdouble-promotion -Wformat=2)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(CodeCoverage)
    append_coverage_compiler_flags()
endif ()

link_libraries(varlink++ asan Catch2::Catch2)
add_compile_options(-fsanitize=address)

function(varlink_test NAME)
    add_executable(test_${NAME} ${ARGN})
    add_test(${NAME} test_${NAME})
endfunction()

function(bash_test NAME SCRIPT)
    configure_file(${SCRIPT} ${SCRIPT})
    add_test(NAME ${NAME} COMMAND ${SCRIPT})
endfunction()

varlink_test(unittests
        unit_client.cpp
        unit_interface.cpp
        unit_message.cpp
        unit_server_session.cpp
        unit_service.cpp
        unit_transport.cpp
        unit_uri.cpp
)
target_link_libraries(test_unittests PRIVATE Catch2::Catch2WithMain)

varlink_test(self_errors self_errors.cpp)
target_link_libraries(test_self_errors PRIVATE Catch2::Catch2WithMain)

add_library(catch_main test_main.cpp)

varlink_test(self_unix self_test_threaded.cpp)
target_link_libraries(test_self_unix PRIVATE catch_main)
target_compile_definitions(test_self_unix PRIVATE VARLINK_TEST_UNIX)
varlink_test(self_tcp self_test_threaded.cpp)
target_link_libraries(test_self_tcp PRIVATE catch_main)
target_compile_definitions(test_self_tcp PRIVATE VARLINK_TEST_TCP)

varlink_test(self_unix_async self_test_async.cpp)
target_link_libraries(test_self_unix_async PRIVATE catch_main)
target_compile_definitions(test_self_unix_async PRIVATE VARLINK_TEST_UNIX VARLINK_TEST_ASYNC)
varlink_test(self_tcp_async self_test_async.cpp)
target_link_libraries(test_self_tcp_async PRIVATE catch_main)
target_compile_definitions(test_self_tcp_async PRIVATE VARLINK_TEST_TCP VARLINK_TEST_ASYNC)

add_executable(cert_client cert_client.cpp)
add_executable(cert_client_async cert_client_async.cpp)

varlink_wrapper(org.varlink.certification.varlink)
add_executable(cert_server cert_server.cpp org.varlink.certification.varlink.hpp)
target_include_directories(cert_server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

bash_test(certification self_cert.sh)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    setup_target_for_coverage_gcovr_html(
            NAME coverage
            EXECUTABLE test_self_unix COMMAND test_self_tcp COMMAND test_self_errors COMMAND test_unittests
            EXCLUDE "${PROJECT_SOURCE_DIR}/3rdparty" "${PROJECT_SOURCE_DIR}/test")
endif ()
