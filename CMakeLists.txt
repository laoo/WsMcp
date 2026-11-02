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
option( WS_BUILD_APP   "Build the SDL3 desktop application"       OFF )
option( WS_WERROR      "Treat compiler warnings as errors"        OFF )

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
