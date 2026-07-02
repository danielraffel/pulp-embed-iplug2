# pulp-embed-iplug2 — iPlug2 adapter for the pulp_view_embed C ABI.
#
# PulpEmbedEditor (include/PulpEmbedEditor.h) is a framework-neutral helper that
# drives the C ABI from an iPlug2 plugin's editor open/resize/close/timer hooks.
# It depends ONLY on pulp_view_embed.h (parent passed as void*), so it compiles
# without the iPlug2 SDK present; this CMake builds a compile/link check against
# the C ABI. Wiring into a real iPlug2 plugin is documented in the README.
#
# Configure:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_PREFIX_PATH=/path/to/pulp-sdk-install \
#     -DPULP_VIEW_EMBED_DIR=/path/to/pulp-view-embed
cmake_minimum_required(VERSION 3.24)
project(pulp_embed_iplug2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PULP_VIEW_EMBED_DIR "" CACHE PATH "Path to the pulp-view-embed checkout")
if(NOT PULP_VIEW_EMBED_DIR OR NOT EXISTS "${PULP_VIEW_EMBED_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set -DPULP_VIEW_EMBED_DIR to the pulp-view-embed checkout")
endif()
add_subdirectory("${PULP_VIEW_EMBED_DIR}" pulp-view-embed-build)

# Header-only adapter exposed as an INTERFACE target for iPlug2 plugins to use.
add_library(pulp_embed_iplug2 INTERFACE)
target_include_directories(pulp_embed_iplug2
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(pulp_embed_iplug2 INTERFACE pulp_view_embed)

# Compile/link check (proves the helper is valid C++ against the ABI).
enable_testing()
add_executable(pulp-embed-iplug2-compilecheck examples/compilecheck/main.cpp)
target_link_libraries(pulp-embed-iplug2-compilecheck PRIVATE pulp_embed_iplug2)
if(APPLE)
    target_link_libraries(pulp-embed-iplug2-compilecheck PRIVATE "-framework AppKit")
endif()
add_test(NAME compilecheck COMMAND pulp-embed-iplug2-compilecheck)

# Param-bridge binding test: drives PulpEmbedEditor's host bridge with a fake
# (duck-typed) iPlug2 delegate against a real materialized design — proves the
# adapter wires the C ABI param bridge to the delegate WITHOUT the iPlug2 SDK.
add_executable(pulp-embed-iplug2-binding-test examples/binding-test/main.cpp)
target_link_libraries(pulp-embed-iplug2-binding-test PRIVATE pulp_embed_iplug2)
target_compile_definitions(pulp-embed-iplug2-binding-test
    PRIVATE PULP_EMBED_FIXTURES_DIR="${PULP_VIEW_EMBED_DIR}/fixtures")
if(APPLE)
    target_link_libraries(pulp-embed-iplug2-binding-test PRIVATE "-framework AppKit")
endif()
add_test(NAME binding-test COMMAND pulp-embed-iplug2-binding-test)

# Native-view real-plugin example scaffold assertion. The example is a real
# iPlug2 plugin (examples/native-view-plugin/) that needs an iPlug2 checkout +
# Pulp SDK install to build, so this test proves the scaffold is coherent and
# wires the adapter surface (native-view factory binding, ABI v8 accessors,
# resize recipe) via file-content assertions — no build required.
add_test(NAME native-view-plugin-scaffold
    COMMAND ${CMAKE_COMMAND}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/examples/native-view-plugin/scaffold_check.cmake)

# Linux: re-mention fontconfig AFTER libskia.a on each Skia-linking executable
# (single-pass GNU ld; the Skia archive references Fc* symbols). The helper is
# exposed by the Pulp SDK's PulpConfig (loaded via the pulp-view-embed subdir's
# find_package(Pulp)); COMMAND-guarded for older SDKs / non-Linux.
if(UNIX AND NOT APPLE AND NOT ANDROID AND COMMAND pulp_link_fontconfig_after_skia)
    pulp_link_fontconfig_after_skia(pulp-embed-iplug2-compilecheck)
    pulp_link_fontconfig_after_skia(pulp-embed-iplug2-binding-test)
endif()
