# What a release package holds: `cmake --install <build> --component wsmcp`.
# The libraries fetched in Dependencies.cmake carry install rules of their own,
# for their headers and archives, and a package wants none of them; so every
# rule here is the `wsmcp` component's, and a package installs that alone.
#
# The package holds the programs, the licence, this project's credits and the
# licence of every library linked into them (THIRD_PARTY.md). On macOS the
# application is an app bundle, assembled here rather than built as one, so
# that the build tree keeps the executable where the scripts and docs find it.

set( WS_COMPONENT wsmcp )

install( TARGETS ws_cli RUNTIME DESTINATION . COMPONENT ${WS_COMPONENT} )

if( WS_BUILD_APP )
  if( APPLE )
    # A bundle's version is numbers alone: the tag's, or 0.0.0 for a build
    # that is not a release.
    set( WS_BUNDLE_VERSION "0.0.0" )
    if( WS_VERSION_STRING MATCHES "^([0-9]+(\\.[0-9]+)*)" )
      set( WS_BUNDLE_VERSION "${CMAKE_MATCH_1}" )
    endif( )
    set( WS_BUNDLE_MINIMUM_SYSTEM "${CMAKE_OSX_DEPLOYMENT_TARGET}" )
    if( NOT WS_BUNDLE_MINIMUM_SYSTEM )
      set( WS_BUNDLE_MINIMUM_SYSTEM "13.0" )
    endif( )
    configure_file( "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in" "${CMAKE_BINARY_DIR}/Info.plist" @ONLY )

    install( TARGETS ws_app RUNTIME DESTINATION WsMcp.app/Contents/MacOS COMPONENT ${WS_COMPONENT} )
    install( FILES "${CMAKE_BINARY_DIR}/Info.plist" DESTINATION WsMcp.app/Contents COMPONENT ${WS_COMPONENT} )
  else( )
    install( TARGETS ws_app RUNTIME DESTINATION . COMPONENT ${WS_COMPONENT} )
  endif( )
endif( )

install( FILES LICENSE README.md THIRD_PARTY.md DESTINATION . COMPONENT ${WS_COMPONENT} )

# Each library's own licence, named after it.
set( licences
     "CLI11.txt=${cli11_SOURCE_DIR}/LICENSE"
     "spdlog.txt=${spdlog_SOURCE_DIR}/LICENSE"
     "nlohmann_json.txt=${nlohmann_json_SOURCE_DIR}/LICENSE.MIT"
     "miniz.txt=${miniz_SOURCE_DIR}/LICENSE"
     "cpp-httplib.txt=${httplib_SOURCE_DIR}/LICENSE" )
if( WS_BUILD_APP )
  list( APPEND licences "SDL3.txt=${sdl3_SOURCE_DIR}/LICENSE.txt" "imgui.txt=${imgui_SOURCE_DIR}/LICENSE.txt" )
endif( )
foreach( entry IN LISTS licences )
  string( REPLACE "=" ";" pair "${entry}" )
  list( GET pair 0 name )
  list( GET pair 1 file )
  install( FILES "${file}" DESTINATION licenses RENAME "${name}" COMPONENT ${WS_COMPONENT} )
endforeach( )
