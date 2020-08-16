add_library(varlink++
        client.cpp
        service.cpp
        connection.cpp
        )

target_include_directories(varlink++ PUBLIC ${PROJECT_SOURCE_DIR}/include)