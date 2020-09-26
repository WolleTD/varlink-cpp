varlink_wrapper(org.varlink.service.varlink)

add_library(varlink++ INTERFACE)

target_include_directories(varlink++
        INTERFACE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/pegtl/include ${CMAKE_CURRENT_BINARY_DIR})
#org.varlink.service.varlink.hpp
