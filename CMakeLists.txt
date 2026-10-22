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

# ###################### Dead-code elimination #######################
# The static dependencies (wxWidgets, kwxFFI, QuickJS-ng) otherwise pull whole
# object files into the executable. One section per function/variable lets the
# linker drop everything unreferenced, which reduces both the binary size and
# the number of pages Windows must fault in before main() runs. These options
# must be in place before FetchContent_MakeAvailable so the dependencies inherit
# them; they are directory-scoped and apply to every target created afterwards.
if(MSVC)
    add_compile_options(/Gy /Gw)
    add_link_options(/OPT:REF /OPT:ICF)
elseif(APPLE)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,-dead_strip)
else()
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
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
# runtime/host_runtime.js is compiled into the executable so it is always
# available without reading from disk.
include(${KWX_TS_DIR}/cmake/EmbedFile.cmake)

kwx_ts_embed_file(
    ${KWX_TS_DIR}/runtime/host_runtime.js
    kwx_host_runtime
    KWX_HOST_RUNTIME_CPP
)

# ###################### Precompiled runtime bytecode #######################
# The two virtual modules (the "kwx" binding corpus and kwx/platform) are
# compiled to QuickJS bytecode at build time by qjsc, taken from the same
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

# kwx_platform.js is a separate CommonJS asset: generated bindings call
# dlopen() at module-eval time, so the corpus bundle must not resolve
# "kwx/platform" to a bundle that is still executing.
add_custom_command(
    OUTPUT ${KWX_BYTECODE_DIR}/kwx_platform_factory.js
    COMMAND ${CMAKE_COMMAND} -E make_directory ${KWX_BYTECODE_DIR}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${KWX_TS_DIR}/runtime/kwx_platform.js
        -DOUTPUT=${KWX_BYTECODE_DIR}/kwx_platform_factory.js
        -DFACTORY_NAME=__kwx_platform_factory
        -P ${KWX_TS_DIR}/cmake/WrapFactory.cmake
    DEPENDS
        ${KWX_TS_DIR}/runtime/kwx_platform.js
        ${KWX_TS_DIR}/cmake/WrapFactory.cmake
    COMMENT "Wrapping kwx_platform.js as a CommonJS factory"
    VERBATIM
)

add_custom_command(
    OUTPUT ${KWX_BYTECODE_DIR}/kwx_platform_bytecode.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${KWX_BYTECODE_DIR}
    COMMAND $<TARGET_FILE:qjsc>
        -C -s
        -N kwx_platform_bytecode
        -n kwx_platform.js
        -o ${KWX_BYTECODE_DIR}/kwx_platform_bytecode.c
        ${KWX_BYTECODE_DIR}/kwx_platform_factory.js
    DEPENDS ${KWX_BYTECODE_DIR}/kwx_platform_factory.js $<TARGET_FILE:qjsc>
    COMMENT "Compiling kwx_platform.js to QuickJS bytecode"
    VERBATIM
)

# ###################### TypeScript transformer (Rust / SWC) #######################
# TypeScript is lowered to CommonJS by kwx_ts/third_party/swc_transformer, a Rust
# crate built as a `staticlib` and linked straight into the executable. That
# replaces the embedded Sucrase bundle: there is no transpiler bytecode to load
# and no JavaScript parser to evaluate before the first module can run.
#
# The crate is compiled from the sources Cargo fetches out of crates.io. The
# repository ships no prebuilt binary, so a Rust toolchain is a build
# requirement; the version of swc_core that gets built is pinned by Cargo.lock
# and its version string is mixed into the transpile-cache key.
find_program(CARGO_EXECUTABLE cargo
    HINTS
        "$ENV{CARGO_HOME}/bin"
        "$ENV{USERPROFILE}/.cargo/bin"
        "$ENV{HOME}/.cargo/bin"
)
if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR
        "cargo was not found, but kwx_ts needs it to build the Rust SWC "
        "transformer in kwx_ts/third_party/swc_transformer. Install a Rust "
        "toolchain from https://rustup.rs and re-run CMake.")
endif()
message(STATUS "Using cargo: ${CARGO_EXECUTABLE}")

set(KWX_SWC_DIR ${KWX_TS_DIR}/third_party/swc_transformer)
# Cargo writes into the build tree so the source tree stays clean and the target
# directory is discarded along with the rest of the build output.
set(KWX_SWC_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/swc-transformer)

# The Rust library must match the C++ toolchain's ABI. On Windows the two are
# independent choices (MSVC / clang-cl on one side, a MinGW-style clang on the
# other), so the triple is derived from the compiler rather than from the host.
# On macOS a Universal build needs one slice per architecture.
set(KWX_SWC_RUST_TRIPLES "")
if(WIN32)
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
            list(APPEND KWX_SWC_RUST_TRIPLES aarch64-pc-windows-msvc)
        else()
            list(APPEND KWX_SWC_RUST_TRIPLES x86_64-pc-windows-msvc)
        endif()
    else()
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
            list(APPEND KWX_SWC_RUST_TRIPLES aarch64-pc-windows-gnullvm)
        else()
            list(APPEND KWX_SWC_RUST_TRIPLES x86_64-pc-windows-gnu)
        endif()
    endif()
elseif(APPLE)
    foreach(arch ${CMAKE_OSX_ARCHITECTURES})
        if(arch STREQUAL "arm64")
            list(APPEND KWX_SWC_RUST_TRIPLES aarch64-apple-darwin)
        else()
            list(APPEND KWX_SWC_RUST_TRIPLES ${arch}-apple-darwin)
        endif()
    endforeach()
    if(NOT KWX_SWC_RUST_TRIPLES)
        list(APPEND KWX_SWC_RUST_TRIPLES ${CMAKE_SYSTEM_PROCESSOR}-apple-darwin)
    endif()
else()
    # Linux and other Unix: cargo's default (host) triple is the right one. The
    # literal "host" keeps the list non-empty, which a bare "" would not.
    list(APPEND KWX_SWC_RUST_TRIPLES host)
endif()

file(GLOB_RECURSE KWX_SWC_RUST_SOURCES CONFIGURE_DEPENDS ${KWX_SWC_DIR}/src/*.rs)

# One cargo invocation per slice. The crate is always built in release mode,
# whatever the executable's configuration: the transformer runs on every module
# load and a debug build of SWC is an order of magnitude slower.
set(KWX_SWC_COMMANDS "")
set(KWX_SWC_ARTIFACTS "")
foreach(triple ${KWX_SWC_RUST_TRIPLES})
    if(triple STREQUAL "host")
        set(target_arguments "")
        set(artifact_directory ${KWX_SWC_TARGET_DIR}/release)
    else()
        set(target_arguments --target ${triple})
        set(artifact_directory ${KWX_SWC_TARGET_DIR}/${triple}/release)
    endif()

    list(APPEND KWX_SWC_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${KWX_SWC_TARGET_DIR}
            ${CARGO_EXECUTABLE} build --release ${target_arguments}
            --manifest-path ${KWX_SWC_DIR}/Cargo.toml
    )
    list(APPEND KWX_SWC_ARTIFACTS
        ${artifact_directory}/${CMAKE_STATIC_LIBRARY_PREFIX}kwx_swc_transformer${CMAKE_STATIC_LIBRARY_SUFFIX}
    )
endforeach()

list(LENGTH KWX_SWC_ARTIFACTS kwx_swc_slice_count)
if(kwx_swc_slice_count GREATER 1)
    # Universal build: merge the per-architecture archives into one.
    set(KWX_SWC_LIBRARY
        ${KWX_SWC_TARGET_DIR}/universal/${CMAKE_STATIC_LIBRARY_PREFIX}kwx_swc_transformer${CMAKE_STATIC_LIBRARY_SUFFIX})
    list(APPEND KWX_SWC_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory ${KWX_SWC_TARGET_DIR}/universal
        COMMAND lipo -create -output ${KWX_SWC_LIBRARY} ${KWX_SWC_ARTIFACTS}
    )
else()
    list(GET KWX_SWC_ARTIFACTS 0 KWX_SWC_LIBRARY)
endif()

add_custom_command(
    OUTPUT ${KWX_SWC_LIBRARY}
    ${KWX_SWC_COMMANDS}
    DEPENDS
        ${KWX_SWC_RUST_SOURCES}
        ${KWX_SWC_DIR}/Cargo.toml
        ${KWX_SWC_DIR}/Cargo.lock
    COMMENT "Building the SWC TypeScript transformer (cargo build --release)"
    VERBATIM
    USES_TERMINAL
)

add_custom_target(kwx_swc_transformer_build DEPENDS ${KWX_SWC_LIBRARY})

add_library(kwx_swc_transformer STATIC IMPORTED GLOBAL)
set_target_properties(kwx_swc_transformer PROPERTIES
    IMPORTED_LOCATION ${KWX_SWC_LIBRARY}
)
add_dependencies(kwx_swc_transformer kwx_swc_transformer_build)

# ###################### kwx_ts executable #######################
add_executable(kwx_ts
    ${KWX_TS_DIR}/src/main.cpp
    ${KWX_TS_DIR}/src/quickjs_host.cpp
    ${KWX_TS_DIR}/src/kwx_host_app.cpp
    ${KWX_TS_DIR}/src/runtime_bindings.cpp
    ${KWX_GENERATED_DIR}/bindings_gen.cpp
    ${KWX_HOST_RUNTIME_CPP}
    ${KWX_BYTECODE_DIR}/kwx_runtime_bytecode.c
    ${KWX_BYTECODE_DIR}/kwx_platform_bytecode.c
)

# The generated header is included both by the generated source and by
# runtime_bindings.cpp, so its directory must be on the include path.
target_include_directories(kwx_ts PRIVATE
    ${KWX_GENERATED_DIR}
    ${KWX_TS_DIR}/src
)

# kwxFFI PUBLIC-links every wxWidgets component, so linking it transitively
# supplies the wxWidgets libraries, headers and platform frameworks.
target_link_libraries(kwx_ts PRIVATE qjs kwxFFI kwx_swc_transformer)

# A Rust `staticlib` carries its own copy of std, and that copy references the
# platform libraries rustc would normally link for a Rust executable. MSVC does
# not read those linker directives out of a static library, so they are listed
# explicitly here. Over-linking these system libraries is harmless.
if(WIN32)
    target_link_libraries(kwx_ts PRIVATE
        advapi32 userenv ws2_32 ntdll bcrypt ole32 shell32
    )
elseif(APPLE)
    target_link_libraries(kwx_ts PRIVATE
        "-framework Security" "-framework CoreFoundation"
    )
else()
    target_link_libraries(kwx_ts PRIVATE dl pthread m)
endif()

# Debug info produced by the static dependencies ends up in the Release
# executable even though none of this project's translation units is built with
# -g. It is dead weight for a shipped binary, so strip it from Release only;
# RelWithDebInfo and Debug keep their symbols.
if(MSVC)
    target_link_options(kwx_ts PRIVATE $<$<CONFIG:Release>:/DEBUG:NONE>)
else()
    target_link_options(kwx_ts PRIVATE $<$<CONFIG:Release>:-Wl,--strip-debug>)
endif()

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

# ###################### Windows resources (manifest, icon, version) #######################
# res/kwx_ts.rc embeds the Common Controls v6 manifest with resource ID 1 (so
# Windows applies visual styles without a runtime activation context), the
# application icon, and the VERSIONINFO block. res/version.rc is generated from
# the project version so the file properties track every build.
if(WIN32)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/res/version.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc
        @ONLY
    )

    target_sources(kwx_ts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/res/kwx_ts.rc)
    target_include_directories(kwx_ts PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

    # Only MSVC / clang-cl accept /MANIFEST:NO. GNU/Clang frontends on MSYS2
    # use a different linker and reject this flag.
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        target_link_options(kwx_ts PRIVATE "/MANIFEST:NO")
    endif()
endif()

# ###################### Native stack size #######################
# The SWC parser, the TypeScript transforms and QuickJS all recurse, and QuickJS
# is configured with an 8 MiB JS stack guard (JS_SetMaxStackSize). The OS main
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

# Windows produces kwxts.exe; macOS/Linux produce kwxts. The executable lands
# in the repository-level bin/<Config> (see CMAKE_RUNTIME_OUTPUT_DIRECTORY
# above), alongside kwxgen and kwx_binding_gen.
set_target_properties(kwx_ts PROPERTIES OUTPUT_NAME kwxts)

# Platform-specific compile definitions collected above (empty where unused).
target_compile_definitions(kwx_ts PRIVATE ${KWX_TS_PLATFORM_DEFINITIONS})

# ###################### Packaging Instructions #######################
# install(TARGETS) places kwxts in bin/, so the NSIS registry entries and the
# installed icon reference bin/kwxts.exe.
set(CPACK_PACKAGE_VENDOR "KeyWorks Software")
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_LIST_DIR}/LICENSE)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "kwx_ts")
set(CPACK_PACKAGE_EXECUTABLES "kwxts" "kwx_ts")
set(CPACK_CREATE_DESKTOP_LINKS kwxts)

set(CPACK_NSIS_MUI_ICON ${CMAKE_CURRENT_LIST_DIR}/res/kwx_ts_logo.ico)
set(CPACK_NSIS_MUI_UNIICO ${CMAKE_CURRENT_LIST_DIR}/res/kwx_ts_logo.ico)
set(CPACK_NSIS_INSTALLED_ICON_NAME bin/kwxts.exe)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL YES)
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "kwxts.exe")
set(CPACK_NSIS_BRANDING_TEXT "KeyWorks Software")
set(CPACK_NSIS_BRANDING_TEXT_TRIM_POSITION CENTER)
set(CPACK_NSIS_MODIFY_PATH YES)

set(CPACK_RPM_PACKAGE_LICENSE ${CMAKE_CURRENT_LIST_DIR}/LICENSE)
set(CPACK_RPM_PACKAGE_VENDOR "KeyWorks Software")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_REQUIRES "libgtk-3.so.0, glibc >= 2.38")
set(CPACK_RPM_PACKAGE_URL "https://github.com/Randalphwa/kwxTypeScript-dev")
set(CPACK_RPM_PACKAGE_DESCRIPTION "TypeScript runtime for wxWidgets")
set(CPACK_RPM_PACKAGE_SUMMARY "kwx_ts TypeScript runtime for wxWidgets")

# Use the build number to determine if the package is newer than the previous build.
set(CPACK_RPM_PACKAGE_RELEASE ${PROJECT_VERSION_BUILD})

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "KeyWorks Software <https://github.com/KeyWorksRW>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgtk-3-0, libc6 (>= 2.31)")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Randalphwa/kwxTypeScript-dev")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "TypeScript runtime for wxWidgets")
set(CPACK_DEBIAN_PACKAGE_ICON ${CMAKE_CURRENT_LIST_DIR}/kwx_ts_logo.svg)
set(CPACK_DEBIAN_PACKAGE_SUMMARY "kwx_ts TypeScript runtime for wxWidgets")

# Use the build number to determine if the package is newer than the previous build.
set(CPACK_DEBIAN_PACKAGE_RELEASE ${PROJECT_VERSION_BUILD})

# Registers .kwxts files with kwxts.exe in the NSIS installer.
configure_file("${CMAKE_CURRENT_LIST_DIR}/nsis_options.cmake.in"
    "${PROJECT_BINARY_DIR}/nsis_options.cmake"
    @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/nsis_options.cmake")

if(WIN32)
    set(CPACK_GENERATOR ZIP NSIS)
elseif(APPLE)
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_DMG_VOLUME_NAME "kwx_ts")
    set(CPACK_DMG_FORMAT "UDBZ") # Compressed DMG
elseif(UNIX)
    set(CPACK_GENERATOR "DEB RPM")
endif()

include(CPack)

install(TARGETS kwx_ts CONFIGURATIONS Release)

set_property(INSTALL "bin/$<TARGET_FILE_NAME:kwx_ts>" PROPERTY CPACK_START_MENU_SHORTCUTS "kwx_ts")
