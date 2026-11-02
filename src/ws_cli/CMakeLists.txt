add_executable( ws_cli src/Main.cpp )

set_target_properties( ws_cli PROPERTIES OUTPUT_NAME wsmcp-cli )

target_link_libraries( ws_cli
  PRIVATE ws::server CLI11::CLI11 ws::warnings )
