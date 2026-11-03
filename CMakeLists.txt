cmake_minimum_required( VERSION 3.25 )

project( WsMcp
  DESCRIPTION "Emulator of the WonderSwan and the WonderSwan Color, driven by people and by agents alike"
  LANGUAGES   C CXX )

list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" )

# No `VERSION` above: the version is the tag, and cmake/Version.cmake says why.
include( Version )
wsResolveVersion( WS_VERSION_STRING )
message( STATUS "WsMcp version: ${WS_VERSION_STRING}" )

set( CMAKE_CXX_STANDARD          23 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS        OFF )
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

option( WS_BUILD_TESTS "Build the test suite"                     ON )
option( WS_BUILD_APP   "Build the SDL3 desktop application"       ON )
option( WS_WERROR      "Treat compiler warnings as errors"        OFF )
option( WS_LTO         "Optimise across files in optimised builds" ON )

# The machine is stepped a cycle at a time through calls between its parts,
# each in a file of its own: optimised across files, it runs about a third
# faster. Debug builds are left alone, for the debugger and for build times.
if( WS_LTO )
  include( CheckIPOSupported )
  check_ipo_supported( RESULT WS_LTO_SUPPORTED OUTPUT WS_LTO_ERROR LANGUAGES CXX )
  if( WS_LTO_SUPPORTED )
    # Fetched dependencies that ask for an older CMake would otherwise ignore it.
    set( CMAKE_POLICY_DEFAULT_CMP0069 NEW )
    foreach( config RELEASE RELWITHDEBINFO MINSIZEREL )
      set( CMAKE_INTERPROCEDURAL_OPTIMIZATION_${config} ON )
    endforeach( )
  else( )
    message( STATUS "WsMcp: no optimisation across files: ${WS_LTO_ERROR}" )
  endif( )
endif( )

include( Warnings )
include( Dependencies )

add_subdirectory( src/ws_core )

# In a browser there is no socket, no thread and no command line: the
# WebAssembly build is the core and its binding to JavaScript, nothing else
# (docs/decisions/0010-the-core-compiles-to-webassembly.md).
if( EMSCRIPTEN )
  add_subdirectory( src/ws_web )
  return( )
endif( )

add_subdirectory( src/ws_server )
add_subdirectory( src/ws_cli )

# The application is the one target that needs SDL and ImGui, so a headless
# build, the suite and CI can leave both out.
if( WS_BUILD_APP )
  add_subdirectory( src/ws_app )
endif( )

if( WS_BUILD_TESTS )
  enable_testing( )
  add_subdirectory( tests )
endif( )

include( Install )
