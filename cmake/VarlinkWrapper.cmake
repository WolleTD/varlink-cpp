set(varlink-wrapper-path ${CMAKE_CURRENT_LIST_DIR})
function(varlink_wrapper VARLINK_INTERFACE)
    add_custom_command(OUTPUT "${VARLINK_INTERFACE}.hpp"
            COMMAND "cmake" ARGS "-P"
            "${varlink-wrapper-path}/varlink-wrapper.cmake"
            "${CMAKE_CURRENT_SOURCE_DIR}/${VARLINK_INTERFACE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${VARLINK_INTERFACE}.hpp"
            DEPENDS "${VARLINK_INTERFACE}"
            )
endfunction()

