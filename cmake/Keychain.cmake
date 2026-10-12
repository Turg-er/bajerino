find_package(keychain 1.3.1 QUIET CONFIG)
if(TARGET keychain::keychain)
    return()
endif()

function(_moltorino_fetch_keychain)
    set(BUILD_TESTS OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(CODE_COVERAGE OFF)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    include(FetchContent)
    set(keychain_options)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
        list(APPEND keychain_options SYSTEM)
    endif()
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        list(APPEND keychain_options EXCLUDE_FROM_ALL)
    endif()
    FetchContent_Declare(keychain
        URL https://github.com/hrantzsch/keychain/archive/refs/tags/v1.3.1.tar.gz
        URL_HASH SHA256=9a9fc5aaa94d03180f1c3735f93d1ebcf9d2925fc915306b8476abc17a3906a9
        ${keychain_options}
    )
    FetchContent_MakeAvailable(keychain)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
       CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # Keychain selects GNU warning flags for clang-cl, which rejects
        # -pedantic. Use the same options as its MSVC build instead.
        set_property(TARGET keychain PROPERTY COMPILE_OPTIONS /W2 /WX)
    endif()
    add_library(keychain::keychain ALIAS keychain)
    configure_file("${keychain_SOURCE_DIR}/LICENSE"
        "${BAJERINO_LICENSES_DIR}/keychain.txt" COPYONLY)
endfunction()

_moltorino_fetch_keychain()
