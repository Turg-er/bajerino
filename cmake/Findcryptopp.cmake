find_path(cryptopp_INCLUDE_DIR NAMES cryptopp/config.h DOC "cryptopp include directory")
find_library(cryptopp_LIBRARY NAMES cryptopp DOC "cryptopp library")

if(cryptopp_INCLUDE_DIR)
    # CRYPTOPP_VERSION has been moved to config_ver.h starting with Crypto++ 8.3
    if(EXISTS ${cryptopp_INCLUDE_DIR}/cryptopp/config_ver.h)
        set(cryptopp_VERSION_HEADER "config_ver.h")
    else()
        set(cryptopp_VERSION_HEADER "config.h")
    endif()
    file(STRINGS ${cryptopp_INCLUDE_DIR}/cryptopp/${cryptopp_VERSION_HEADER} _config_version REGEX "CRYPTOPP_VERSION")
    string(REGEX MATCH "([0-9])([0-9])([0-9])" _match_version "${_config_version}")
    set(cryptopp_VERSION_STRING "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(cryptopp
    REQUIRED_VARS cryptopp_INCLUDE_DIR cryptopp_LIBRARY
    FOUND_VAR cryptopp_FOUND
    VERSION_VAR cryptopp_VERSION_STRING)

if(cryptopp_FOUND AND NOT TARGET cryptopp::cryptopp)
    add_library(cryptopp::cryptopp UNKNOWN IMPORTED)
    set_target_properties(cryptopp::cryptopp PROPERTIES
        IMPORTED_LOCATION "${cryptopp_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${cryptopp_INCLUDE_DIR}")
endif()

mark_as_advanced(cryptopp_INCLUDE_DIR cryptopp_LIBRARY)
set(cryptopp_INCLUDE_DIRS ${cryptopp_INCLUDE_DIR})
set(cryptopp_LIBRARIES ${cryptopp_LIBRARY})
