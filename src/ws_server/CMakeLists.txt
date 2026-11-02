add_library( ws_server
  src/JsonLinesServer.cpp
  src/McpHttpServer.cpp
  src/McpServer.cpp
  src/TcpLineServer.cpp )

add_library( ws::server ALIAS ws_server )

target_include_directories( ws_server PUBLIC include )

target_link_libraries( ws_server
  PUBLIC  ws::core
  PRIVATE ws::warnings httplib Threads::Threads $<$<PLATFORM_ID:Windows>:ws2_32> )
