if(NOT CMAKE_ARGC EQUAL 5)
    message(FATAL_ERROR "Usage: varlink-wrapper.cmake <in-file> <out-file>")
endif()

get_filename_component(varlink_basename ${CMAKE_ARGV3} NAME)
get_filename_component(output_dirname ${CMAKE_ARGV4} PATH)
file(MAKE_DIRECTORY "${output_dirname}")
string(REPLACE "." "_" cstring_name ${varlink_basename})
string(TOUPPER ${cstring_name}_H guard_name)

file(READ ${CMAKE_ARGV3} varlink_if)
set(varlink_cxxstring "#ifndef ${guard_name}\n#define ${guard_name}\n#include <string_view>\nnamespace varlink {\n")
string(APPEND varlink_cxxstring
        "inline constexpr const std::string_view ${cstring_name} = R\"INTERFACE(\n${varlink_if})INTERFACE\"\;\n")
string(APPEND varlink_cxxstring "} // namespace varlink\n#endif // ${guard_name}\n")
file(WRITE ${CMAKE_ARGV4} ${varlink_cxxstring})
