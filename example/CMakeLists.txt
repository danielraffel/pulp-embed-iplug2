# Minimal iPlug2 standalone (APP) that embeds a Pulp-imported design via the
# pulp_view_embed C ABI. No IGraphics (UI NONE), no Swift — the editor is the
# embedded Pulp native view, attached in PulpEmbedIPlug::OpenWindow.
#
# Configure (Release):
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
#     -DIPLUG2_DIR=/path/to/iPlug2 \
#     -DPULP_VIEW_EMBED_DIR=/path/to/pulp-view-embed \
#     -DCMAKE_PREFIX_PATH=/path/to/pulp-sdk-install
cmake_minimum_required(VERSION 3.16)
project(PulpEmbedIPlug VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

if(NOT DEFINED IPLUG2_DIR OR NOT EXISTS "${IPLUG2_DIR}/iPlug2.cmake")
  message(FATAL_ERROR "Set -DIPLUG2_DIR to an iPlug2 checkout (with Dependencies built)")
endif()
set(PULP_VIEW_EMBED_DIR "" CACHE PATH "Path to the pulp-view-embed checkout")
if(NOT PULP_VIEW_EMBED_DIR OR NOT EXISTS "${PULP_VIEW_EMBED_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Set -DPULP_VIEW_EMBED_DIR to the pulp-view-embed checkout")
endif()

# The embedding C ABI (separate repo) — provides the pulp_view_embed target.
add_subdirectory("${PULP_VIEW_EMBED_DIR}" pulp-view-embed-build)

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    PulpEmbedIPlug.h
    PulpEmbedIPlug.mm
    resources/resource.h
  FORMATS APP
  UI NONE
  LINK pulp_view_embed
  # UI NONE drops the IGraphics lib but doesn't define NO_IGRAPHICS, which the
  # editor-delegate selector (IPlugDelegate_select.h) needs to pick the plain
  # IEditorDelegate instead of IGraphicsEditorDelegate.
  DEFINES NO_IGRAPHICS
)

# Wire the helper header + bake the demo design path onto every produced target.
set(_ir "${PULP_VIEW_EMBED_DIR}/fixtures/figma-vst-style/design.ir.json")
foreach(_t ${PROJECT_NAME}-app)
  if(TARGET ${_t})
    target_include_directories(${_t} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    target_compile_definitions(${_t} PRIVATE PULP_EMBED_DEMO_IR="${_ir}")
    target_link_libraries(${_t} PRIVATE "-framework AppKit")
  endif()
endforeach()
