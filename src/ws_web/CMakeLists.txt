# The core for a browser: a JavaScript module, wsmcp.js and wsmcp.wasm, that
# a page imports (docs/decisions/0010-the-core-compiles-to-webassembly.md).
add_executable( ws_web src/Bindings.cpp )

set_target_properties( ws_web PROPERTIES OUTPUT_NAME wsmcp SUFFIX ".js" )

# A module the page instantiates when it wants, with the in-memory file
# system the cartridge and the boot ROM are written to, and memory that grows
# with the history of execution.
target_link_options( ws_web PRIVATE
  -lembind
  "-sMODULARIZE=1"
  "-sEXPORT_ES6=1"
  "-sEXPORT_NAME=createWsMcp"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sENVIRONMENT=web,worker,node"
  "-sEXPORTED_RUNTIME_METHODS=FS" )

target_link_libraries( ws_web PRIVATE ws::core ws::warnings )
