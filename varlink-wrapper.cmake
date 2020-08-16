if(NOT CMAKE_ARGC EQUAL 5)
    message(FATAL_ERROR "Usage: varlink-wrapper.cmake <in-file> <out-file>")
endif()

get_filename_component(varlink_basename ${CMAKE_ARGV3} NAME)
string(REPLACE "." "_" cstring_name ${varlink_basename})

file(READ ${CMAKE_ARGV3} varlink_if)
set(varlink_cxxstring
    "constexpr std::string_view ${cstring_name} = R\"INTERFACE(\n${varlink_if})INTERFACE\"\;\n")
file(WRITE ${CMAKE_ARGV4} ${varlink_cxxstring})
