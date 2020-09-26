set(CMAKE_INCLUDE_CURRENT_DIR ON)
varlink_wrapper(org.varlink.service.varlink)

add_library(varlink++
        org.varlink.service.varlink.hpp
        interface.cpp)

target_include_directories(varlink++
        PUBLIC ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/pegtl/include)