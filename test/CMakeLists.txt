add_subdirectory(googletest EXCLUDE_FROM_ALL)

link_libraries(varlink++)

add_executable(test_unittests
        unit_client.cpp
        unit_interface.cpp
        unit_message.cpp
        unit_service.cpp
        unit_transport.cpp
        unit_uri.cpp
        unit_socket.cpp
        unit_server.cpp
        )
target_link_libraries(test_unittests PRIVATE gtest gmock gtest_main)
add_test(unittests test_unittests)

add_executable(test_sockets unix-socket.cpp)
target_link_libraries(test_sockets PRIVATE gtest gmock gtest_main)
add_test(sockets test_sockets)
