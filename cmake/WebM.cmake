find_package(webm QUIET CONFIG)
if(TARGET webm::webm)
    return()
endif()

find_path(WEBM_INCLUDE_DIR webm/webm_parser.h)
find_library(WEBM_LIBRARY NAMES webm)
if(WEBM_INCLUDE_DIR AND WEBM_LIBRARY)
    add_library(webm::webm UNKNOWN IMPORTED)
    set_target_properties(webm::webm PROPERTIES
        IMPORTED_LOCATION "${WEBM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBM_INCLUDE_DIR}")
    return()
endif()

# The incremental parser is not included in every distribution's libwebm.
function(bajerino_fetch_webm)
    FetchContent_Declare(bajerino_webm
        URL https://github.com/webmproject/libwebm/archive/refs/tags/libwebm-1.0.0.32.tar.gz
        URL_HASH SHA256=7fd5e085bda9f8031cf2ad2a1e52d9b7b29cba9c0b96ad2ce794ce89e4249eb8
        SOURCE_SUBDIR unused)
    FetchContent_MakeAvailable(bajerino_webm)
    # Only the incremental parser is needed. libwebm's legacy build system
    # changes global compiler flags, so keep this dependency target-scoped.
    file(GLOB parser_sources "${bajerino_webm_SOURCE_DIR}/webm_parser/src/*.cc")
    add_library(bajerino-webm-parser STATIC ${parser_sources})
    target_include_directories(bajerino-webm-parser SYSTEM PUBLIC
        "${bajerino_webm_SOURCE_DIR}/webm_parser/include")
    target_include_directories(bajerino-webm-parser PRIVATE
        "${bajerino_webm_SOURCE_DIR}/webm_parser")
    set_target_properties(bajerino-webm-parser PROPERTIES
        POSITION_INDEPENDENT_CODE ON AUTOMOC OFF)
    add_library(webm::webm ALIAS bajerino-webm-parser)
    configure_file("${bajerino_webm_SOURCE_DIR}/LICENSE.TXT"
        "${BAJERINO_LICENSES_DIR}/libwebm.txt" COPYONLY)
endfunction()

bajerino_fetch_webm()
