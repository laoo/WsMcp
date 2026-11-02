// The headless command line is deliberately thin: it parses arguments, wires a
// transport to the dispatcher and hands over. Everything testable lives in
// ws_core and ws_server.

#include "ws/Emulator.hpp"
#include "ws/Version.hpp"
#include "ws/control/Batch.hpp"
#include "ws/control/Dispatcher.hpp"
#include "ws/server/JsonLinesServer.hpp"
#include "ws/server/McpServer.hpp"
#include "ws/server/TcpLineServer.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/// `answer` with the build's version written `devel`, as a transcript holds
/// it, so that any build checks a transcript alike.
std::string asRecorded( std::string answer )
{
  std::string const version{ ws::versionString() };
  for ( std::size_t at = answer.find( version ); at != std::string::npos; at = answer.find( version, at ) )
  {
    answer.replace( at, version.size(), "devel" );
    at += 5;
  }
  return answer;
}

/// Answers the requests of the transcript at `path`, one a line, as JSON
/// lines or as MCP messages, and prints the answers; or, given `expected`,
/// compares them with its lines (docs/spec/control-protocol.md §1).
int runTranscript( ws::control::Dispatcher const& dispatcher,
                   std::filesystem::path const& path,
                   bool mcp,
                   std::filesystem::path const& expected )
{
  std::ifstream requests{ path };
  if ( !requests )
  {
    std::cerr << "wsmcp-cli: cannot read " << path.string() << '\n';
    return 2;
  }
  ws::server::JsonLinesServer const lines{ dispatcher };
  ws::server::McpServer mcpServer{ dispatcher };
  std::vector<std::string> answers;
  std::string line;
  while ( std::getline( requests, line ) )
  {
    if ( !line.empty() && line.back() == '\r' )
    {
      line.pop_back();
    }
    if ( line.empty() || line.starts_with( '#' ) )
    {
      continue;
    }
    if ( !mcp )
    {
      answers.push_back( asRecorded( lines.handleLine( line ) ) );
    }
    else if ( auto answer = mcpServer.handleLine( line ) )
    {
      answers.push_back( asRecorded( std::move( *answer ) ) );
    }
  }

  if ( expected.empty() )
  {
    for ( std::string const& answer : answers )
    {
      std::cout << answer << '\n';
    }
    return 0;
  }
  std::ifstream file{ expected };
  std::vector<std::string> recorded;
  while ( std::getline( file, line ) )
  {
    if ( !line.empty() && line.back() == '\r' )
    {
      line.pop_back();
    }
    recorded.push_back( line );
  }
  for ( std::size_t i = 0; i < std::max( answers.size(), recorded.size() ); ++i )
  {
    std::string const got = i < answers.size() ? answers[i] : "(no answer)";
    std::string const want = i < recorded.size() ? recorded[i] : "(no answer)";
    if ( got != want )
    {
      std::cout << fmt::format(
          "{}: answer {} differs\n  expected: {}\n  got:      {}\n", path.filename().string(), i + 1, want, got );
      return 1;
    }
  }
  std::cout << fmt::format( "{}: {} answers match\n", path.filename().string(), answers.size() );
  return 0;
}

/// What --batch exits with when its script needs a cartridge, a boot ROM or a file that is
/// not there: what CTest takes for a skipped test.
constexpr int EXIT_SKIPPED = 77;

/// Runs the batch script at `path`, docs/spec/batch.md, and checks its sums
/// against the golden ones the script holds, or records them into it.
int runScript( ws::Settings settings, std::filesystem::path const& path, bool record )
{
  std::ifstream file{ path };
  auto script = ws::control::Json::parse( file, nullptr, false );
  if ( script.is_discarded() || !script.is_object() )
  {
    std::cerr << "wsmcp-cli: " << path.string() << " is not a JSON object\n";
    return 2;
  }
  auto const result = ws::control::runBatch( std::move( settings ), script );
  switch ( result.outcome )
  {
  case ws::control::BatchResult::Outcome::MISSING:
    std::cout << path.filename().string() << ": skipped: " << result.message << '\n';
    return EXIT_SKIPPED;
  case ws::control::BatchResult::Outcome::FAILED:
    std::cerr << path.filename().string() << ": " << result.message << '\n';
    return 2;
  case ws::control::BatchResult::Outcome::RAN:
    break;
  }

  if ( record )
  {
    script["golden"] = ws::control::goldenOf( result );
    std::ofstream out{ path };
    out << script.dump( 2 ) << '\n';
    std::cout << path.filename().string() << ": recorded " << result.checkpoints.size() << " checkpoints\n";
    return out ? 0 : 2;
  }
  auto const differences = ws::control::compareWithGolden(
      result, script.contains( "golden" ) ? script.at( "golden" ) : ws::control::Json{} );
  for ( std::string const& difference : differences )
  {
    std::cout << path.filename().string() << ": " << difference << '\n';
  }
  if ( differences.empty() )
  {
    std::cout << path.filename().string() << ": " << result.checkpoints.size() << " checkpoints match\n";
  }
  return differences.empty() ? 0 : 1;
}

int run( int argc, char** argv )
{
  CLI::App app{ "Headless WonderSwan and WonderSwan Color emulator", "wsmcp-cli" };
  app.set_version_flag( "--version", std::string{ ws::versionString() } );

  ws::Settings settings;
  app.add_option( "cartridge", settings.cartridge, "The cartridge's ROM, a .ws or .wsc file" )
      ->check( CLI::ExistingFile );
  app.add_option( "--bios-dir",
                  settings.biosDirectory,
                  "Where the boot ROMs are, as bootrom.ws, bootrom.wsc and bootrom_sc.wsc or under No-Intro's names" )
      ->check( CLI::ExistingDirectory );
  bool noBootRom = false;
  app.add_flag( "--no-boot-rom", noBootRom, "Start the cartridge as the boot ROM would leave it, without running it" );
  std::string model;
  app.add_option( "--model", model, "ws, wsc or sc; chosen by the cartridge if not given" )
      ->check( CLI::IsMember( { "ws", "wsc", "sc" } ) );
  app.add_option( "--save-dir",
                  settings.saveDirectory,
                  "Where the cartridge's SRAM or EEPROM and the console's EEPROM are kept between runs" );
  app.add_option( "--state-dir",
                  settings.stateDirectory,
                  "Where state.save and state.load keep save states given by name; the working directory if not given" )
      ->check( CLI::ExistingDirectory );

  std::string serverSpec;
  auto* const serverFlag =
      app.add_option( "--server",
                      serverSpec,
                      "Answer JSON-lines requests: on stdin and stdout, or with tcp:PORT on 127.0.0.1:PORT until "
                      "the process is ended" )
          ->expected( 0, 1 );
  bool mcp = false;
  app.add_flag( "--mcp", mcp, "Serve the Model Context Protocol on stdio, for an agent" )->excludes( serverFlag );
  std::filesystem::path transcript;
  auto* const transcriptOption =
      app.add_option( "--transcript",
                      transcript,
                      "Answer the requests in a file, one a line (MCP messages with --mcp), and print the answers" )
          ->check( CLI::ExistingFile )
          ->excludes( serverFlag );
  std::filesystem::path batch;
  auto* const batchOption =
      app.add_option( "--batch", batch, "Run a batch script (docs/spec/batch.md) and check it against its golden sums" )
          ->check( CLI::ExistingFile )
          ->excludes( serverFlag )
          ->excludes( transcriptOption );
  bool record = false;
  app.add_flag( "--record", record, "With --batch: write the sums into the script as its golden ones" )
      ->needs( batchOption );
  std::filesystem::path expected;
  app.add_option( "--expect", expected, "With --transcript: compare the answers with this file's lines instead" )
      ->check( CLI::ExistingFile )
      ->needs( transcriptOption );

  CLI11_PARSE( app, argc, argv );
  settings.useBootRom = !noBootRom;
  if ( !model.empty() )
  {
    settings.model = ws::modelFromName( model );
  }

  if ( !serverSpec.empty() && !serverSpec.starts_with( "tcp:" ) )
  {
    std::cerr << "wsmcp-cli: --server: expected tcp:PORT, not " << serverSpec << '\n';
    return 1;
  }
  std::uint16_t port = 0;
  if ( !serverSpec.empty() )
  {
    std::string_view const digits = std::string_view{ serverSpec }.substr( 4 );
    auto const [end, error] = std::from_chars( digits.data(), digits.data() + digits.size(), port );
    if ( digits.empty() || error != std::errc{} || end != digits.data() + digits.size() )
    {
      std::cerr << "wsmcp-cli: --server: expected tcp:PORT, a port from 0 to 65535, not " << serverSpec << '\n';
      return 1;
    }
  }

  // stdout carries the protocol and nothing else, so every log line goes to
  // stderr; a client parsing stdout must never meet one.
  spdlog::set_default_logger( spdlog::stderr_color_mt( "wsmcp" ) );

  bool const server = serverFlag->count() > 0;
  if ( !batch.empty() )
  {
    return runScript( std::move( settings ), batch, record );
  }
  if ( !server && !mcp && transcript.empty() )
  {
    std::cerr << app.help();
    return 1;
  }

  ws::Emulator emulator{ std::move( settings ) };
  ws::control::Dispatcher const dispatcher{ emulator };
  if ( !transcript.empty() )
  {
    return runTranscript( dispatcher, transcript, mcp, expected );
  }
  if ( mcp )
  {
    ws::server::McpServer transport{ dispatcher };
    spdlog::info( "wsmcp-cli {} serving MCP on stdio", ws::versionString() );
    transport.serve( std::cin, std::cout );
    return 0;
  }
  if ( !serverSpec.empty() )
  {
    // Connections are served on threads of their own; the dispatcher answers
    // one request at a time.
    std::mutex oneAtATime;
    ws::server::TcpLineServer const transport{ port,
                                               [&]( ws::control::Json const& request )
                                               {
                                                 std::scoped_lock const lock{ oneAtATime };
                                                 return dispatcher.handle( request );
                                               } };
    spdlog::info( "wsmcp-cli {} serving JSON-lines on 127.0.0.1:{}", ws::versionString(), transport.port() );
    for ( ;; )
    {
      std::this_thread::sleep_for( std::chrono::hours{ 1 } );
    }
  }
  ws::server::JsonLinesServer const transport{ dispatcher };
  spdlog::info( "wsmcp-cli {} serving JSON-lines on stdio", ws::versionString() );
  transport.serve( std::cin, std::cout );
  return 0;
}

} // namespace

// Nothing may escape main: an unhandled exception would abort without telling
// the user anything useful, and CLI11 reports argument problems by throwing.
// The handlers write to stderr directly rather than through spdlog, because
// logging may allocate or throw and would then be the second failure in a row.
int main( int argc, char** argv )
{
  try
  {
    return run( argc, argv );
  }
  catch ( std::exception const& error )
  {
    std::fprintf( stderr, "wsmcp-cli: %s\n", error.what() );
    return 2;
  }
  catch ( ... )
  {
    std::fputs( "wsmcp-cli: internal error: unknown exception\n", stderr );
    return 2;
  }
}
