# Real iPlug2 plugin (APP + VST3 + AU + CLAP) whose editor is a HAND-BUILT
# compiled Pulp View (the NATIVE-VIEW path) rather than an importer bundle. It
# exercises the full adapter surface end to end: the NativeViewFactory binding
# ctor, the ABI v8 accessors (param display text + host actions), and the P3
# resize recipe. See README.md.
#
# Configure (Release):
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
#     -DIPLUG2_DIR=/path/to/iPlug2 \
#     -DPULP_VIEW_EMBED_DIR=/path/to/pulp-view-embed \
#     -DCMAKE_PREFIX_PATH=/path/to/pulp-sdk-install
cmake_minimum_required(VERSION 3.16)
project(PulpNativeViewIPlug VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

if(NOT DEFINED IPLUG2_DIR OR NOT EXISTS "${IPLUG2_DIR}/iPlug2.cmake")
  message(FATAL_ERROR "Set -DIPLUG2_DIR to an iPlug2 checkout (with Dependencies built)")
endif()
set(PULP_VIEW_EMBED_DIR "" CACHE PATH "Path to the pulp-view-embed checkout")
if(NOT PULP_VIEW_EMBED_DIR OR NOT EXISTS "${PULP_VIEW_EMBED_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Set -DPULP_VIEW_EMBED_DIR to the pulp-view-embed checkout")
endif()

# The embedding C ABI (separate repo) — provides pulp_view_embed AND, via its
# find_package(Pulp), the Pulp SDK include path this example's native-view TU
# needs for <pulp/view/design_frame_view.hpp>.
add_subdirectory("${PULP_VIEW_EMBED_DIR}" pulp-view-embed-build)

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    NativeViewPlugin.h
    NativeViewPlugin.mm
    resources/resource.h
  FORMATS APP VST3 AU CLAP
  UI NONE
  LINK pulp_view_embed
  DEFINES NO_IGRAPHICS
)

foreach(_t ${PROJECT_NAME}-app ${PROJECT_NAME}-vst3 ${PROJECT_NAME}-au ${PROJECT_NAME}-clap)
  if(TARGET ${_t})
    target_include_directories(${_t} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
    target_link_libraries(${_t} PRIVATE "-framework AppKit")
  endif()
endforeach()
