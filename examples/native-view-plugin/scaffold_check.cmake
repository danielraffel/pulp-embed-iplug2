# File-content assertions for the native-view-plugin scaffold.
#
# The example is a REAL iPlug2 plugin but cannot be built without an iPlug2
# checkout + a Pulp SDK install, so this test proves the scaffold is coherent and
# wires the load-bearing adapter surface (native-view factory binding, ABI v8
# accessors, P3 resize recipe). Run via `cmake -P scaffold_check.cmake` — no
# build required. Fails loudly (FATAL_ERROR) on any missing file/marker.

set(_dir "${CMAKE_CURRENT_LIST_DIR}")

foreach(_f config.h NativeViewPlugin.h NativeViewPlugin.mm CMakeLists.txt README.md
           resources/resource.h)
  if(NOT EXISTS "${_dir}/${_f}")
    message(FATAL_ERROR "native-view-plugin scaffold missing file: ${_f}")
  endif()
endforeach()

file(READ "${_dir}/NativeViewPlugin.mm" _mm)
file(READ "${_dir}/NativeViewPlugin.h" _hh)
file(READ "${_dir}/CMakeLists.txt" _cml)

# Native-view factory binding ctor with the key->paramIdx map.
foreach(_marker
    "NativeViewFactory"                # the compiled-View mount path
    "DesignFrameView"                  # a real Pulp View is built
    "\"gain\", kGain"                  # design key -> IParam index binding
    "boundParameterCount")             # bind self-check in the self-check
  string(FIND "${_mm}" "${_marker}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "NativeViewPlugin.mm missing native-view binding marker: ${_marker}")
  endif()
endforeach()

# ABI v8 accessors + P3 resize recipe are actually wired.
foreach(_marker
    "setParamDisplayFormatter"         # v8 param_display_text seam
    "GetDisplayForHost"                # routed through iPlug2 IParam::GetDisplay
    "setHostActionHandler"             # v8 host_action seam
    "applyPreferredSizeOnOpen"         # P3 size-on-open
    "constrainSize")                   # P3 host-resize clamp
  string(FIND "${_mm}" "${_marker}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "NativeViewPlugin.mm missing adapter-surface marker: ${_marker}")
  endif()
endforeach()

# Real plugin target (all four formats), not a stub.
string(FIND "${_cml}" "FORMATS APP VST3 AU CLAP" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "CMakeLists.txt does not declare a real APP+VST3+AU+CLAP plugin")
endif()

# The two bound params are declared.
string(FIND "${_hh}" "kGain" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "NativeViewPlugin.h missing kGain param enum")
endif()

message(STATUS "native-view-plugin scaffold OK")
