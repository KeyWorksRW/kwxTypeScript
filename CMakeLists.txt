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

# ###################### One MSVC runtime for every target #######################
# Must be set before project(). QuickJS-ng, kwxFFI, wxWidgets and kwx_ts must all
# use the same dynamically linked CRT (/MD). Mixing /MD and /MDd across static
# libraries causes link errors and heap corruption.
if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "MSVC runtime library for all targets" FORCE)
endif()

project(kwxTypeScript
    LANGUAGES C CXX
    VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_BUILD}
    DESCRIPTION "TypeScript FFI bindings for wxWidgets via kwxFFI"
    HOMEPAGE_URL "https://github.com/Randalphwa/kwxTypeScript-dev"
)

# ###################### General Settings #######################
set(CMAKE_CXX_STANDARD 23)
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

# ###################### macOS Universal binary #######################
# Default to a Universal binary when the caller did not pick architectures.
# This MUST run before any FetchContent_MakeAvailable so that wxWidgets,
# kwxFFI, and QuickJS-ng are all built for both slices.
if(APPLE)
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS Universal binary" FORCE)
        message(STATUS "Defaulting macOS architectures to ${CMAKE_OSX_ARCHITECTURES}")
    endif()
endif()

# ###################### Static-only build #######################
# kwx_ts is a single self-contained executable, so QuickJS-ng, kwxFFI and
# wxWidgets must all be static libraries. Every option below has to be set
# before FetchContent_MakeAvailable so that no dependency can default to SHARED.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(KWXFFI_BUILD_SHARED OFF CACHE BOOL "Build kwxFFI as a static library" FORCE)
set(wxBUILD_SHARED OFF CACHE BOOL "Build wxWidgets as static libraries" FORCE)
set(wxBUILD_USE_STATIC_RUNTIME OFF CACHE BOOL "Use the dynamically linked CRT" FORCE)

# ###################### QuickJS-ng via FetchContent #######################
include(FetchContent)

# Suppress verbose FetchContent git output (CMake 3.30+)
set(FETCHCONTENT_QUIET ON)

# QuickJS-ng exposes the static library target `qjs`. Its examples/tests
# (qjs_exe, qjsc, api-test, lre-test) have no opt-out option and belong only to
# the default `all` target, so every build command must pass `--target kwx_ts`.
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "Disable QuickJS-ng install rules" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "Disable QuickJS-ng example programs" FORCE)

message(STATUS "Fetching QuickJS-ng (this may take awhile) ...")

FetchContent_Declare(
    quickjs
    GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
    GIT_TAG v0.17.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(quickjs)

message(NOTICE "QuickJS-ng has been fetched and configured.")
message(STATUS "QuickJS-ng sources: ${quickjs_SOURCE_DIR}")

# ###################### kwxFFI/wxWidgets via FetchContent #######################
# kwxFFI itself fetches wxWidgets statically as a sub-dependency.
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

# ###################### Source layout #######################
# The kwx_ts C++ sources, CMake helpers and build-time assets live in kwx_ts/.
# The TypeScript corpus consumed by the binding generator stays in the
# repository root (src/ and wx/).
set(KWX_TS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/kwx_ts)

# ###################### Platform support #######################
set(KWX_TS_PLATFORM_DEFINITIONS)
if(WIN32)
    enable_language(RC)
elseif(APPLE)
    # wxWidgets' own build already sets this for its targets; repeat it so the
    # kwx_ts translation units see the same standard-library extension surface.
    add_compile_definitions(__STDC_WANT_LIB_EXT1__=1)
elseif(UNIX)
    # GTK-based wxWidgets build. pkg-config, gtk+-3.0 and x11 development
    # packages must be installed to configure/build on Linux.
    list(APPEND KWX_TS_PLATFORM_DEFINITIONS __WXGTK__)
else()
    message(FATAL_ERROR "Unsupported platform: only WIN32, APPLE, or UNIX are supported.")
endif()

# ###################### Executable output directory #######################
# Set after the FetchContent targets so it only affects this project's own
# executables: kwx_binding_gen and kwx_ts are placed in the repository-level
# bin/<Config> alongside kwxgen -- never in kwx_ts/bin/<Config>. $<CONFIG>
# keeps multi-config generators from appending a second configuration directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<CONFIG>")

# ###################### Binding generator (build-time host tool) #######################
# Parses the TypeScript FFI declaration corpus and emits the native thunks. It
# links nothing but the C++ runtime, so it stays independent of wxWidgets.
add_executable(kwx_binding_gen ${KWX_TS_DIR}/tools/gen_bindings.cpp)
set_target_properties(kwx_binding_gen PROPERTIES OUTPUT_NAME kwx_binding_gen)

target_compile_features(kwx_binding_gen PRIVATE cxx_std_23)

# Generated binding sources live in the build tree, never in kwx_ts/src/.
# $<CONFIG> keeps the output distinct for every configuration so multi-config
# generators do not collide on the same file.
set(KWX_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>)
set(KWX_FFI_GEN_TS ${CMAKE_CURRENT_SOURCE_DIR}/wx/kwx_ffi_gen.ts)
set(KWX_APP_GEN_TS ${CMAKE_CURRENT_SOURCE_DIR}/src/kwxApp.ts)

add_custom_command(
    OUTPUT
        ${KWX_GENERATED_DIR}/bindings_gen.h
        ${KWX_GENERATED_DIR}/bindings_gen.cpp
    COMMAND kwx_binding_gen
        ${KWX_FFI_GEN_TS}
        ${KWX_APP_GEN_TS}
        ${KWX_GENERATED_DIR}
    DEPENDS
        kwx_binding_gen
        ${KWX_FFI_GEN_TS}
        ${KWX_APP_GEN_TS}
    COMMENT "Generating kwxFFI native bindings"
    VERBATIM
)

# ###################### Embedded runtime assets #######################
# runtime/deno_shim.js is compiled into the executable so it is always
# available without reading from disk.
include(${KWX_TS_DIR}/cmake/EmbedFile.cmake)

kwx_ts_embed_file(
    ${KWX_TS_DIR}/runtime/deno_shim.js
    kwx_deno_shim
    KWX_DENO_SHIM_CPP
)

# ###################### Precompiled runtime bytecode #######################
# The two large assets (the Sucrase transpiler and the virtual "kwx" module)
# are compiled to QuickJS bytecode at build time by qjsc, taken from the same
# QuickJS-ng source as the runtime. JS_ReadObject + JS_EvalFunction then load
# them ~20x faster than parsing the source on every launch, and the executable
# carries no source text for either asset.
#
# Bytecode is tied to the QuickJS version and build, so it is generated per
# configuration: $<CONFIG> keeps Debug and Release artifacts apart.
set(KWX_BYTECODE_DIR ${CMAKE_CURRENT_BINARY_DIR}/bytecode/$<CONFIG>)

# kwx_runtime.js is a CommonJS module body, so qjsc compiles a generated wrapper
# that publishes the factory function on globalThis. WrapFactory.cmake explains
# why the body cannot be compiled directly.
add_custom_command(
    OUTPUT ${KWX_BYTECODE_DIR}/kwx_runtime_factory.js
    COMMAND ${CMAKE_COMMAND} -E make_directory ${KWX_BYTECODE_DIR}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${KWX_TS_DIR}/runtime/kwx_runtime.js
        -DOUTPUT=${KWX_BYTECODE_DIR}/kwx_runtime_factory.js
        -DFACTORY_NAME=__kwx_runtime_factory
        -P ${KWX_TS_DIR}/cmake/WrapFactory.cmake
    DEPENDS
        ${KWX_TS_DIR}/runtime/kwx_runtime.js
        ${KWX_TS_DIR}/cmake/WrapFactory.cmake
    COMMENT "Wrapping kwx_runtime.js as a CommonJS factory"
    VERBATIM
)

# -C forces classic-script compilation (qjsc otherwise auto-detects an ES
# module from import/export tokens). -s strips the source text but keeps the
# debug info, so stack traces still carry line numbers.
add_custom_command(
    OUTPUT ${KWX_BYTECODE_DIR}/kwx_runtime_bytecode.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${KWX_BYTECODE_DIR}
    COMMAND $<TARGET_FILE:qjsc>
        -C -s
        -N kwx_runtime_bytecode
        -n kwx_runtime.js
        -o ${KWX_BYTECODE_DIR}/kwx_runtime_bytecode.c
        ${KWX_BYTECODE_DIR}/kwx_runtime_factory.js
    # $<TARGET_FILE:qjsc> is a file-level dependency: a QuickJS upgrade must
    # regenerate the bytecode, because bytecode is tied to the engine build.
    DEPENDS ${KWX_BYTECODE_DIR}/kwx_runtime_factory.js $<TARGET_FILE:qjsc>
    COMMENT "Compiling kwx_runtime.js to QuickJS bytecode"
    VERBATIM
)

add_custom_command(
    OUTPUT ${KWX_BYTECODE_DIR}/sucrase_bytecode.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${KWX_BYTECODE_DIR}
    COMMAND $<TARGET_FILE:qjsc>
        -C -s
        -N kwx_sucrase_bytecode
        -n sucrase.bundle.js
        -o ${KWX_BYTECODE_DIR}/sucrase_bytecode.c
        ${KWX_TS_DIR}/third_party/sucrase.bundle.js
    DEPENDS
        ${KWX_TS_DIR}/third_party/sucrase.bundle.js
        $<TARGET_FILE:qjsc>
    COMMENT "Compiling sucrase.bundle.js to QuickJS bytecode"
    VERBATIM
)

# ###################### kwx_ts executable #######################
add_executable(kwx_ts
    ${KWX_TS_DIR}/src/main.cpp
    ${KWX_TS_DIR}/src/quickjs_host.cpp
    ${KWX_TS_DIR}/src/kwx_host_app.cpp
    ${KWX_TS_DIR}/src/runtime_bindings.cpp
    ${KWX_GENERATED_DIR}/bindings_gen.cpp
    ${KWX_DENO_SHIM_CPP}
    ${KWX_BYTECODE_DIR}/kwx_runtime_bytecode.c
    ${KWX_BYTECODE_DIR}/sucrase_bytecode.c
)

# The generated header is included both by the generated source and by
# runtime_bindings.cpp, so its directory must be on the include path.
target_include_directories(kwx_ts PRIVATE
    ${KWX_GENERATED_DIR}
    ${KWX_TS_DIR}/src
)

# kwxFFI PUBLIC-links every wxWidgets component, so linking it transitively
# supplies the wxWidgets libraries, headers and platform frameworks.
target_link_libraries(kwx_ts PRIVATE qjs kwxFFI)

# C declarations for the kwxFFI wrappers (wxFrame_Create, wxString_CreateUTF8, ...).
target_include_directories(kwx_ts PRIVATE ${kwxFFI_SOURCE_DIR}/include)

# The wxWidgets include roots, mirroring build/_deps/kwxffi-src/CMakeLists.txt.
target_include_directories(kwx_ts PRIVATE ${wxWidgets_SOURCE_DIR}/include)
if(WIN32)
    target_include_directories(kwx_ts PRIVATE
        ${wxWidgets_BINARY_DIR}/lib/vc_x64_lib/mswu
    )
elseif(APPLE)
    target_include_directories(kwx_ts PRIVATE
        ${wxWidgets_BINARY_DIR}/lib/osx_cocoa-unicode-static
    )
elseif(UNIX)
    target_include_directories(kwx_ts PRIVATE
        ${wxWidgets_BINARY_DIR}/lib/gtk3_unixu-3.3/include
    )
endif()

# ###################### Windows manifest (Common Controls v6) #######################
# The executable embeds its own manifest with resource ID 1, so Windows applies
# Common Controls v6 visual styles without any runtime activation context.
if(WIN32)
    target_sources(kwx_ts PRIVATE ${KWX_TS_DIR}/resources/kwx_ts.rc)

    # Only MSVC / clang-cl accept /MANIFEST:NO. GNU/Clang frontends on MSYS2
    # use a different linker and reject this flag.
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        target_link_options(kwx_ts PRIVATE "/MANIFEST:NO")
    endif()
endif()

# ###################### Native stack size #######################
# The embedded Sucrase transpiler recurses during parsing, and QuickJS is
# configured with an 8 MiB JS stack guard (JS_SetMaxStackSize). The OS main
# thread stack must therefore be at least as large, or the OS guard fires as a
# hard STATUS_STACK_OVERFLOW before QuickJS can raise a catchable RangeError.
# The MSVC default is 1 MiB, which is far too small.
if(WIN32)
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        target_link_options(kwx_ts PRIVATE "/STACK:33554432")
    else()
        # GNU-style frontend (MinGW / clang with the GNU driver), as used by
        # quickjs-ng itself for its own executables.
        target_link_options(kwx_ts PRIVATE "-Wl,--stack,33554432")
    endif()
endif()

# Windows produces kwx_ts.exe; macOS/Linux produce kwx_ts. The executable lands
# in the repository-level bin/<Config> (see CMAKE_RUNTIME_OUTPUT_DIRECTORY
# above), alongside kwxgen and kwx_binding_gen.
set_target_properties(kwx_ts PROPERTIES OUTPUT_NAME kwx_ts)

# Platform-specific compile definitions collected above (empty where unused).
target_compile_definitions(kwx_ts PRIVATE ${KWX_TS_PLATFORM_DEFINITIONS})
