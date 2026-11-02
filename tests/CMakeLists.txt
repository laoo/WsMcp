add_executable( ws_tests
  VersionTest.cpp
  control/DispatcherTest.cpp
  cpu/CpuTest.cpp
  machine/ApuTest.cpp
  machine/MachineTest.cpp
  machine/PpuTest.cpp
  machine/RtcTest.cpp
  machine/TestRomsTest.cpp
  server/JsonLinesServerTest.cpp
  server/McpServerTest.cpp
  server/NetworkServersTest.cpp
  support/Files.cpp )

# The core's private headers as well as its public ones: the devices of the
# machine are tested one by one, without the CPU in front of them.
target_include_directories( ws_tests PRIVATE . "${PROJECT_SOURCE_DIR}/src/ws_core/src" )

# miniz's headers only: its code arrives with ws_core, which links it.
# Linking it here as well puts the archive on the link line twice, which
# Apple's linker warns about.
target_include_directories( ws_tests SYSTEM PRIVATE $<TARGET_PROPERTY:miniz,INTERFACE_INCLUDE_DIRECTORIES> )

# Where the [roms] tests find the boot ROMs and the cartridges; no ROM is
# carried in the tree, and the tests skip without them. The owner keeps them
# in ~/Documents/WS (CLAUDE.md, "The workspace around it").
set( WS_TEST_BIOS_DIR "$ENV{HOME}/Documents/WS/BIOS" CACHE PATH "The boot ROMs the tests run" )
set( WS_TEST_ROM_DIR "$ENV{HOME}/Documents/WS/ROM" CACHE PATH "The cartridges the tests run" )
target_compile_definitions( ws_tests
  PRIVATE WS_TEST_BIOS_DIR="${WS_TEST_BIOS_DIR}"
          WS_TEST_ROM_DIR="${WS_TEST_ROM_DIR}" )

# Where the [test-roms] tests find the hardware test ROMs
# scripts/fetch-test-roms.sh puts there; they skip without them.
target_compile_definitions( ws_tests
  PRIVATE WS_TEST_ROM_TOOLS_DIR="${CMAKE_SOURCE_DIR}/build/tools/testroms" )

target_link_libraries( ws_tests
  PRIVATE ws::server Catch2::Catch2WithMain ws::warnings httplib )

include( Catch )
catch_discover_tests( ws_tests )

# The control protocol's transcripts: each one's requests answered by
# wsmcp-cli --transcript and compared with the answers recorded beside it
# (docs/spec/control-protocol.md §1).
foreach( transcript IN ITEMS lines mcp )
  set( transport )
  if( transcript STREQUAL "mcp" )
    set( transport --mcp )
  endif()
  add_test( NAME "control.${transcript}"
            COMMAND ws_cli --transcript "control/${transcript}.jsonl" ${transport}
                    --expect "control/${transcript}.expect"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" )
  set_tests_properties( "control.${transcript}" PROPERTIES LABELS control )
endforeach()

# The regression suite: every script in regression/ run by wsmcp-cli --batch,
# which checks it against the golden sums it holds (docs/spec/batch.md). A
# script whose boot ROM or cartridge is not there exits 77, which CTest counts
# as skipped. They run in the cartridges' directory, which they name their
# cartridges in; without it there is nothing to run.
set( regression_scripts )
if( EXISTS "${WS_TEST_ROM_DIR}" )
  file( GLOB regression_scripts CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/regression/*.json" )
endif()
foreach( script IN LISTS regression_scripts )
  get_filename_component( name "${script}" NAME_WE )
  add_test( NAME "regression.${name}"
            COMMAND ws_cli --batch "${script}" --bios-dir "${WS_TEST_BIOS_DIR}"
            WORKING_DIRECTORY "${WS_TEST_ROM_DIR}" )
  set_tests_properties( "regression.${name}" PROPERTIES LABELS regression SKIP_RETURN_CODE 77 )
endforeach()
