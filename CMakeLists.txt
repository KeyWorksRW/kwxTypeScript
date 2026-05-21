# CMake 3.30 required for FetchContent performance improvements (CMP0168)
cmake_minimum_required(VERSION 3.30)

string(TIMESTAMP YEAR "%y")
string(TIMESTAMP DOY "%j")
string(TIMESTAMP PROJECT_YEAR "%Y")
math(EXPR BUILD_NUM "${YEAR}%100 * 1000 + ${DOY}")

set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION_BUILD ${BUILD_NUM})
set(PROJECT_VERSION_YEAR ${PROJECT_YEAR})

project(kwxTypeScript
    LANGUAGES C CXX
    VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_BUILD}
    DESCRIPTION "TypeScript FFI bindings for wxWidgets via kwxFFI"
    HOMEPAGE_URL "https://github.com/Randalphwa/kwxTypeScript-dev"
)

# ###################### General Settings #######################
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# ###################### Options #######################

# Only generate compile_commands.json for local development
if ( NOT DEFINED ENV{CI} )
    # creates a compile_commands.json file in the build directory, suitable for clangd
    set( CMAKE_EXPORT_COMPILE_COMMANDS ON )
endif()

# ###################### sccache support #######################
find_program(SCCACHE_PROGRAM sccache)

if(SCCACHE_PROGRAM)
    message(STATUS "Using sccache: ${SCCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
endif()

# ###################### kwxFFI/wxWidgets via FetchContent #######################
include(FetchContent)

# Suppress verbose FetchContent git output (CMake 3.30+)
set(FETCHCONTENT_QUIET ON)

# Build kwxFFI as a SHARED library for Deno.dlopen().
# Deno FFI loads the .dll/.so/.dylib at runtime — no custom host executable needed.
# wxWidgets remains static regardless.
set(KWXFFI_BUILD_SHARED ON CACHE BOOL "Build kwxFFI as shared library" FORCE)

message(STATUS "Fetching kwxFFI and wxWidgets (this may take awhile) ...")

FetchContent_Declare(
    kwxFFI
    GIT_REPOSITORY https://github.com/KeyWorksRW/kwxFFI.git
    GIT_TAG origin/main
    GIT_SHALLOW TRUE
    UPDATE_DISCONNECTED FALSE
)

message(NOTICE "Configuring kwxFFI...")

FetchContent_MakeAvailable(kwxFFI)

message(NOTICE "kwxFFI has been fetched and configured.")
message(STATUS "kwxFFI sources: ${kwxFFI_SOURCE_DIR}")

# Define wxWidgets paths (kwxFFI fetches wxWidgets as a subdependency)
set(WX_SRC_DIR ${CMAKE_BINARY_DIR}/_deps/wxwidgets-src)
set(WX_BUILD_DIR ${CMAKE_BINARY_DIR}/_deps/wxwidgets-build)
message(STATUS "wxWidgets sources: ${WX_SRC_DIR}")
message(STATUS "wxWidgets build: ${WX_BUILD_DIR}")

# kwxFFI builds into ${CMAKE_BINARY_DIR}/../lib — i.e. build/lib/ relative to source root.
# The shared library (kwxFFI.dll / libkwxFFI.so / libkwxFFI.dylib) lands there.

# ###################### kwxApp — Application Lifecycle Layer #######################
# kwxApp provides the pure-C app initialization that Deno FFI needs:
# kwxApp_Initialize, kwxApp_MainLoop, etc.
#
# Local copies in src/ (originally from kwxFFI examples/CApp/) so they can be
# modified as needed without depending on a specific kwxFFI fetch commit.
set(KWXAPP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(EXISTS ${KWXAPP_SRC_DIR}/kwxApp.cpp)
    message(STATUS "Adding kwxApp (from src/) to kwxFFI shared library")

    # Add kwxApp sources to the existing kwxFFI target
    target_sources(kwxFFI PRIVATE
        ${KWXAPP_SRC_DIR}/kwxApp.cpp
        ${KWXAPP_SRC_DIR}/kwxApp.h
    )

    # kwxApp needs its own include directory for the header
    target_include_directories(kwxFFI PRIVATE ${KWXAPP_SRC_DIR})

    # Define KWXAPP_EXPORTS so kwxApp.h uses dllexport
    target_compile_definitions(kwxFFI PRIVATE KWXAPP_EXPORTS)
else()
    message(WARNING "kwxApp not found at ${KWXAPP_SRC_DIR} — app initialization will not be available")
endif()

# ###################### Windows Manifest for Common Controls v6 #######################
# Enable visual styles (ComCtl32.dll v6) by embedding a manifest into the
# shared library. When Deno.dlopen() loads kwxFFI.dll, Windows checks the
# DLL's manifest and activates Common Controls v6 for the process.
if(WIN32)
    # Locate the manifest files relative to KWXAPP_SRC_DIR (src/)
    set(KWXFFI_MANIFEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

    if(EXISTS ${KWXFFI_MANIFEST_DIR}/kwxFFI_manifest.rc AND
       EXISTS ${KWXFFI_MANIFEST_DIR}/kwxFFI.manifest)
        target_sources(kwxFFI PRIVATE
            ${KWXFFI_MANIFEST_DIR}/kwxFFI_manifest.rc
        )
        message(STATUS "Embedding Common Controls v6 manifest into kwxFFI shared library")
    else()
        message(WARNING "Manifest files not found at ${KWXFFI_MANIFEST_DIR} — "
            "visual styles (ComCtl32 v6) will not be enabled")
    endif()
endif()

# Copy the DLL to lib/ for convenient discovery by Deno at runtime.
message(NOTICE "kwxFFI shared library will be available in the 'lib/' directory.")
message(NOTICE "Run: deno run --allow-ffi --allow-read --allow-write myapp.ts")
