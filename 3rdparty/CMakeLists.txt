include(FetchContent)

FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent
        GIT_TAG v3.9.1)

FetchContent_GetProperties(json)
if(NOT json_POPULATED AND NOT VARLINK_USE_EXTERNAL_JSON)
    FetchContent_Populate(json)
    add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest
        GIT_TAG origin/master)

FetchContent_GetProperties(googletest)
if(NOT googletest_POPULATED AND VARLINK_BUILD_TESTS)
    FetchContent_Populate(googletest)
    add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(pegtl
        GIT_REPOSITORY https://github.com/taocpp/pegtl
        GIT_TAG origin/master)

FetchContent_GetProperties(pegtl)
if(NOT pegtl_POPULATED AND NOT VARLINK_USE_EXTERNAL_PEGTL)
    FetchContent_Populate(pegtl)
    add_subdirectory(${pegtl_SOURCE_DIR} ${pegtl_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
