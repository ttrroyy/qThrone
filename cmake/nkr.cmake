# Release version: prefer an explicit -DINPUT_VERSION=... cache variable, then env.
if (NOT DEFINED INPUT_VERSION)
    set(INPUT_VERSION "$ENV{INPUT_VERSION}")
endif ()
set(NKR_VERSION "${INPUT_VERSION}")
set(QTHRONE_REPOSITORY "$ENV{GITHUB_REPOSITORY}" CACHE STRING "qThrone GitHub repository (owner/name)")
if (QTHRONE_REPOSITORY STREQUAL "throneproj/Throne")
    set(QTHRONE_REPOSITORY "")
endif ()

# Derive numeric parts for Windows PE VERSIONINFO (X.Y.Z.W) from NKR_VERSION.
# Examples:
#   1.2.3       -> 1.2.3.0
#   1.2.3-beta -> 1.2.3.0
#   v1.2.3      -> 1.2.3.0
set(NKR_VERSION_NUMERIC_STR "${NKR_VERSION}")
string(REGEX REPLACE "^[vV]" "" NKR_VERSION_NUMERIC_STR "${NKR_VERSION_NUMERIC_STR}")
string(REGEX REPLACE "-.*$" "" NKR_VERSION_NUMERIC_STR "${NKR_VERSION_NUMERIC_STR}")

# Defaults match generate_product_version() current fallback: 1.0.0.0
set(NKR_VERSION_MAJOR 1)
set(NKR_VERSION_MINOR 0)
set(NKR_VERSION_PATCH 0)
set(NKR_VERSION_REVISION 0)

# CMake's regex engine has no {n,m} quantifier -- braces match literally, so the
# part count is bounded by the list(GET) guards below rather than by the pattern.
if (NOT NKR_VERSION_NUMERIC_STR MATCHES "^[0-9]+(\\.[0-9]+)*$")
    if (NKR_VERSION MATCHES "([0-9]+\\.[0-9]+(\\.[0-9]+)*)")
        set(NKR_VERSION_NUMERIC_STR "${CMAKE_MATCH_1}")
    endif ()
endif ()

if (NKR_VERSION_NUMERIC_STR MATCHES "^[0-9]+(\\.[0-9]+)*$")
    string(REPLACE "." ";" _nkr_parts "${NKR_VERSION_NUMERIC_STR}")

    list(LENGTH _nkr_parts _nkr_len)
    if (_nkr_len GREATER_EQUAL 1)
        list(GET _nkr_parts 0 NKR_VERSION_MAJOR)
    endif ()
    if (_nkr_len GREATER_EQUAL 2)
        list(GET _nkr_parts 1 NKR_VERSION_MINOR)
    endif ()
    if (_nkr_len GREATER_EQUAL 3)
        list(GET _nkr_parts 2 NKR_VERSION_PATCH)
    endif ()
    if (_nkr_len GREATER_EQUAL 4)
        list(GET _nkr_parts 3 NKR_VERSION_REVISION)
    endif ()
endif ()

message(STATUS "NKR_VERSION='${NKR_VERSION}' -> ${NKR_VERSION_MAJOR}.${NKR_VERSION_MINOR}.${NKR_VERSION_PATCH}.${NKR_VERSION_REVISION}")

configure_file("${CMAKE_SOURCE_DIR}/cmake/NkrVersion.h.in" "${CMAKE_BINARY_DIR}/NkrVersion.h" @ONLY)

# Debug
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DNKR_CPP_DEBUG")

# Func
function(nkr_add_compile_definitions arg)
    message("[add_compile_definitions] ${ARGV}")
    add_compile_definitions(${ARGV})
endfunction()
