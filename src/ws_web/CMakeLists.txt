# The core for a browser: a JavaScript module, wsmcp.js and wsmcp.wasm, that
# a page imports (docs/decisions/0010-the-core-compiles-to-webassembly.md),
# put beside the page in web/ (docs/decisions/0014-the-page-keeps-its-files-in-indexeddb.md):
# build/wasm/web is the whole site.
add_executable( ws_web src/Bindings.cpp )

set_target_properties( ws_web PROPERTIES
  OUTPUT_NAME wsmcp
  SUFFIX ".js"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web" )

# A module the page instantiates when it wants, with the in-memory file
# system the cartridge and the boot ROM are written to, IndexedDB's under the
# directories the page keeps, and memory that grows with the history of
# execution.
target_link_options( ws_web PRIVATE
  -lembind
  -lidbfs.js
  "-sMODULARIZE=1"
  "-sEXPORT_ES6=1"
  "-sEXPORT_NAME=createWsMcp"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sENVIRONMENT=web,worker,node"
  "-sEXPORTED_RUNTIME_METHODS=FS,IDBFS" )

target_link_libraries( ws_web PRIVATE ws::core ws::warnings )

# The page's own files, copied whenever one changes.
set( WS_PAGE_FILES index.html style.css main.js input.js sound.js sound-worklet.js )
set( WS_PAGE_OUTPUTS )
foreach( file IN LISTS WS_PAGE_FILES )
  add_custom_command(
    OUTPUT  "${CMAKE_BINARY_DIR}/web/${file}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_SOURCE_DIR}/web/${file}" "${CMAKE_BINARY_DIR}/web/${file}"
    DEPENDS "${PROJECT_SOURCE_DIR}/web/${file}"
    VERBATIM )
  list( APPEND WS_PAGE_OUTPUTS "${CMAKE_BINARY_DIR}/web/${file}" )
endforeach( )
add_custom_target( ws_page ALL DEPENDS ${WS_PAGE_OUTPUTS} )
