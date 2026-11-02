#include "DebuggerWindows.hpp"

#include <imgui.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <charconv>
#include <string_view>
#include <utility>

namespace ws::app
{

namespace
{

constexpr char const* NO_MACHINE = "No cartridge is in";

/// The answer's result when it succeeded, null otherwise.
control::Json resultOf( control::Json const& response )
{
  return response.at( "ok" ) == true ? response.at( "result" ) : control::Json{};
}

std::string errorOf( control::Json const& response )
{
  return response.at( "ok" ) == true ? std::string{} : response.at( "error" ).at( "message" ).get<std::string>();
}

/// An address typed as "SSSS:OOOO" or as linear hex, answered when Enter is
/// pressed.
bool inputAddress( char const* label, AddressText& text )
{
  ImGui::SetNextItemWidth( ImGui::CalcTextSize( "FFFF:FFFF__" ).x + ( ImGui::GetStyle().FramePadding.x * 2 ) );
  return ImGui::InputTextWithHint( label,
                                   "SSSS:OOOO",
                                   text.data(),
                                   text.size(),
                                   ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue );
}

/// What was typed, as the protocol takes an address: hex digits alone are
/// linear, and get the `$` it asks for.
control::Json addressOf( AddressText const& text )
{
  std::string typed{ text.data() };
  if ( typed.contains( ':' ) || typed.starts_with( '$' ) || typed.starts_with( "0X" ) )
  {
    return typed;
  }
  return "$" + typed;
}

/// The linear address typed, 0 when it cannot be read.
std::uint32_t linearOf( AddressText const& text )
{
  auto const hex = []( std::string_view digits )
  {
    std::uint32_t value = 0;
    while ( digits.starts_with( '$' ) || digits.starts_with( ' ' ) )
    {
      digits.remove_prefix( 1 );
    }
    if ( digits.starts_with( "0X" ) )
    {
      digits.remove_prefix( 2 );
    }
    std::from_chars( digits.data(), digits.data() + digits.size(), value, 16 );
    return value;
  };
  std::string_view const typed{ text.data() };
  auto const colon = typed.find( ':' );
  if ( colon == std::string_view::npos )
  {
    return hex( typed );
  }
  return ( ( hex( typed.substr( 0, colon ) ) * 16 ) + hex( typed.substr( colon + 1 ) ) ) & 0xFFFFFU;
}

/// The flags as `ODITSZAPC`, a capital for each set, as cpu.get_state gives
/// them.
std::string flagsText( unsigned flags )
{
  static constexpr std::array<std::pair<char, unsigned>, 9> FLAGS{
    { { 'O', 11 }, { 'D', 10 }, { 'I', 9 }, { 'T', 8 }, { 'S', 7 }, { 'Z', 6 }, { 'A', 4 }, { 'P', 2 }, { 'C', 0 } }
  };
  std::string text;
  for ( auto const& [name, bit] : FLAGS )
  {
    text += ( flags & ( 1U << bit ) ) != 0 ? name : static_cast<char>( name - 'A' + 'a' );
  }
  return text;
}

/// A value of io.get_state's answer, under `name`: an object as a node of the
/// tree, a register the PPU latches as its value and latch, numbers in hex and
/// decimal.
void drawValue( std::string const& name, control::Json const& value )
{
  if ( value.is_object() && value.size() == 2 && value.contains( "value" ) && value.contains( "latch" ) &&
       value.at( "value" ).is_number_integer() )
  {
    ImGui::Text( "%s: $%llX, latched $%llX",
                 name.c_str(),
                 value.at( "value" ).get<unsigned long long>(),
                 value.at( "latch" ).get<unsigned long long>() );
  }
  else if ( value.is_object() || ( value.is_array() && !std::ranges::all_of( value, &control::Json::is_number ) ) )
  {
    if ( ImGui::TreeNode( name.c_str() ) )
    {
      std::size_t index = 0;
      for ( auto const& [key, item] : value.items() )
      {
        drawValue( value.is_array() ? fmt::format( "{}", index ) : key, item );
        ++index;
      }
      ImGui::TreePop();
    }
  }
  else if ( value.is_array() )
  {
    std::string numbers;
    for ( auto const& item : value )
    {
      numbers += fmt::format( "{}{:02X}", numbers.empty() ? "" : " ", item.get<unsigned long long>() );
    }
    ImGui::TextWrapped( "%s: %s", name.c_str(), numbers.c_str() );
  }
  else if ( value.is_number_integer() )
  {
    auto const number = value.get<long long>();
    ImGui::Text( "%s: $%llX (%lld)", name.c_str(), static_cast<unsigned long long>( number ), number );
  }
  else if ( value.is_number() )
  {
    ImGui::Text( "%s: %g", name.c_str(), value.get<double>() );
  }
  else if ( value.is_boolean() )
  {
    ImGui::Text( "%s: %s", name.c_str(), value.get<bool>() ? "yes" : "no" );
  }
  else if ( value.is_string() )
  {
    ImGui::Text( "%s: %s", name.c_str(), value.get_ref<std::string const&>().c_str() );
  }
}

} // namespace

CpuWindow::CpuWindow( Requester request ) : mRequest{ std::move( request ) } {}

void CpuWindow::draw( bool& open )
{
  if ( !open )
  {
    return;
  }
  if ( !ImGui::Begin( "CPU", &open ) )
  {
    ImGui::End();
    return;
  }
  control::Json const state = resultOf( mRequest( "cpu.get_state", control::Json::object() ) );
  if ( state.is_null() )
  {
    ImGui::TextUnformatted( NO_MACHINE );
    ImGui::End();
    return;
  }

  bool const paused = resultOf( mRequest( "emu.status", control::Json::object() ) ).value( "paused", false );
  if ( ImGui::Button( paused ? "Run" : "Pause" ) )
  {
    static_cast<void>( mRequest( paused ? "emu.resume" : "emu.pause", control::Json::object() ) );
  }
  ImGui::SameLine();
  ImGui::BeginDisabled( !paused );
  if ( ImGui::Button( "Step" ) )
  {
    static_cast<void>( mRequest( "emu.step", control::Json::object() ) );
  }
  ImGui::SameLine();
  if ( ImGui::Button( "Step frame" ) )
  {
    static_cast<void>( mRequest( "emu.run_frames", { { "count", 1 } } ) );
  }
  ImGui::EndDisabled();

  auto const word = [&state]( char const* name ) { return state.at( name ).get<unsigned>(); };
  ImGui::Text( "AX %04X  BX %04X  CX %04X  DX %04X", word( "ax" ), word( "bx" ), word( "cx" ), word( "dx" ) );
  ImGui::Text( "SI %04X  DI %04X  BP %04X  SP %04X", word( "si" ), word( "di" ), word( "bp" ), word( "sp" ) );
  ImGui::Text( "CS %04X  DS %04X  ES %04X  SS %04X", word( "cs" ), word( "ds" ), word( "es" ), word( "ss" ) );
  ImGui::Text( "IP %04X  %s%s",
               word( "ip" ),
               state.at( "flags_text" ).get_ref<std::string const&>().c_str(),
               state.at( "halted" ) == true ? "  halted" : "" );
  ImGui::Text( "Cycle %llu", static_cast<unsigned long long>( state.at( "cycle" ).get<std::uint64_t>() ) );

  // The code from CS:IP on, in CS; a click on a line sets or clears its
  // breakpoint.
  ImGui::SeparatorText( "Code" );
  control::Json const breakpoints = resultOf( mRequest( "debug.breakpoint.list", control::Json::object() ) )
                                        .value( "breakpoints", control::Json::array() );
  control::Json const code =
      resultOf( mRequest( "cpu.disassemble", { { "address", state.at( "cs_ip" ) }, { "count", 24 } } ) );
  if ( !code.is_null() )
  {
    for ( auto const& instruction : code.at( "instructions" ) )
    {
      auto const address = instruction.at( "address" ).get<unsigned>();
      bool const breaking = std::ranges::any_of(
          breakpoints, [address]( control::Json const& breakpoint ) { return breakpoint.get<unsigned>() == address; } );
      std::string const line = fmt::format( "{} {}  {:<12} {}",
                                            breaking ? '*' : ' ',
                                            instruction.at( "cs_ip" ).get<std::string>(),
                                            instruction.at( "bytes_hex" ).get<std::string>(),
                                            instruction.at( "text" ).get<std::string>() );
      if ( ImGui::Selectable( line.c_str(), address == word( "pc" ) ) )
      {
        mLastError = errorOf(
            mRequest( breaking ? "debug.breakpoint.remove" : "debug.breakpoint.add", { { "address", address } } ) );
      }
    }
  }
  drawBreakpoints();

  ImGui::End();
}

void CpuWindow::drawBreakpoints()
{
  ImGui::SeparatorText( "Breakpoints" );
  bool add = inputAddress( "##breakpoint", mNewBreakpoint );
  ImGui::SameLine();
  add |= ImGui::Button( "Add" );
  if ( add && mNewBreakpoint[0] != 0 )
  {
    mLastError = errorOf( mRequest( "debug.breakpoint.add", { { "address", addressOf( mNewBreakpoint ) } } ) );
  }
  control::Json const breakpoints = resultOf( mRequest( "debug.breakpoint.list", control::Json::object() ) )
                                        .value( "breakpoints", control::Json::array() );
  for ( auto const& breakpoint : breakpoints )
  {
    auto const address = breakpoint.get<unsigned>();
    ImGui::PushID( static_cast<int>( address ) );
    ImGui::Text( "$%05X", address );
    ImGui::SameLine();
    if ( ImGui::SmallButton( "Remove" ) )
    {
      mLastError = errorOf( mRequest( "debug.breakpoint.remove", { { "address", address } } ) );
    }
    ImGui::PopID();
  }
  if ( !mLastError.empty() )
  {
    ImGui::TextWrapped( "%s", mLastError.c_str() );
  }
}

MemoryWindow::MemoryWindow( Requester request ) : mRequest{ std::move( request ) } {}

void MemoryWindow::draw( bool& open )
{
  if ( !open )
  {
    return;
  }
  if ( !ImGui::Begin( "Memory", &open ) )
  {
    ImGui::End();
    return;
  }
  control::Json const regions =
      resultOf( mRequest( "memory.list_regions", control::Json::object() ) ).value( "regions", control::Json::array() );
  std::size_t size = 0;
  ImGui::SetNextItemWidth( ImGui::GetFontSize() * 10.0F );
  if ( ImGui::BeginCombo( "##region", mRegion.c_str() ) )
  {
    for ( auto const& region : regions )
    {
      auto const& name = region.at( "name" ).get_ref<std::string const&>();
      if ( ImGui::Selectable( name.c_str(), name == mRegion ) )
      {
        mRegion = name;
        mLastError.clear();
      }
    }
    ImGui::EndCombo();
  }
  for ( auto const& region : regions )
  {
    if ( region.at( "name" ) == mRegion && region.at( "size" ).is_number() )
    {
      size = region.at( "size" ).get<std::size_t>();
    }
  }
  ImGui::SameLine();
  ImGui::TextUnformatted( "Go to" );
  ImGui::SameLine();
  if ( inputAddress( "##goto", mGoTo ) )
  {
    mScrollToGoTo = true;
  }
  if ( size == 0 )
  {
    ImGui::TextUnformatted( regions.empty() ? NO_MACHINE : "The region is empty" );
    ImGui::End();
    return;
  }
  if ( !mLastError.empty() )
  {
    ImGui::TextWrapped( "%s", mLastError.c_str() );
  }

  constexpr std::size_t bytesPerRow = 16;
  int const rows = static_cast<int>( ( size + bytesPerRow - 1 ) / bytesPerRow );
  int const digits = size > 0x10000 ? 5 : 4;
  if ( ImGui::BeginChild( "bytes", ImVec2{ 0.0F, 0.0F }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar ) )
  {
    float const rowHeight = ImGui::GetTextLineHeightWithSpacing();
    if ( mScrollToGoTo )
    {
      std::size_t const row = std::min<std::size_t>( linearOf( mGoTo ), size - 1 ) / bytesPerRow;
      ImGui::SetScrollY( static_cast<float>( row ) * rowHeight );
      mScrollToGoTo = false;
    }
    ImGuiListClipper clipper;
    clipper.Begin( rows, rowHeight );
    while ( clipper.Step() )
    {
      // The rows on show, in one request.
      auto const first = static_cast<std::size_t>( clipper.DisplayStart ) * bytesPerRow;
      std::size_t const count =
          std::min( static_cast<std::size_t>( clipper.DisplayEnd - clipper.DisplayStart ) * bytesPerRow, size - first );
      control::Json const response =
          mRequest( "memory.read", { { "region", mRegion }, { "address", first }, { "size", count } } );
      control::Json const read = resultOf( response );
      if ( read.is_null() )
      {
        mLastError = errorOf( response );
        // As many rows as the clipper asked for, as it measures them.
        for ( int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row )
        {
          ImGui::TextUnformatted( "" );
        }
        continue;
      }
      auto const& hex = read.at( "data_hex" ).get_ref<std::string const&>();
      for ( std::size_t at = 0; at < count; at += bytesPerRow )
      {
        std::string line = fmt::format( "{:0{}X} ", first + at, digits );
        std::string characters;
        for ( std::size_t column = 0; column < bytesPerRow && at + column < count; ++column )
        {
          std::string_view const pair = std::string_view{ hex }.substr( ( at + column ) * 2, 2 );
          unsigned value = 0;
          std::from_chars( pair.data(), pair.data() + pair.size(), value, 16 );
          line += ' ';
          line += pair;
          characters += value >= 0x20 && value < 0x7f ? static_cast<char>( value ) : '.';
        }
        line += "  ";
        line += characters;
        ImGui::TextUnformatted( line.c_str() );
      }
    }
  }
  ImGui::EndChild();
  ImGui::End();
}

HistoryWindow::HistoryWindow( Requester request, std::function<void( double seconds )> keep )
    : mRequest{ std::move( request ) }, mKeep{ std::move( keep ) }
{
}

void HistoryWindow::draw( bool& open )
{
  if ( !open )
  {
    return;
  }
  if ( !ImGui::Begin( "History", &open ) )
  {
    ImGui::End();
    return;
  }
  control::Json const kept = resultOf( mRequest( "debug.trace", { { "count", 0 } } ) );
  if ( kept.is_null() )
  {
    ImGui::TextUnformatted( NO_MACHINE );
    ImGui::End();
    return;
  }
  auto const total = kept.at( "total" ).get<int>();
  if ( !mSecondsKnown )
  {
    mSeconds = kept.at( "seconds" ).get<double>();
    mSecondsKnown = true;
  }
  ImGui::TextUnformatted( "Keep" );
  ImGui::SameLine();
  ImGui::SetNextItemWidth( ImGui::GetFontSize() * 6.0F );
  ImGui::InputDouble( "seconds", &mSeconds, 0.5, 2.0, "%.2f" );
  if ( ImGui::IsItemDeactivatedAfterEdit() )
  {
    mSeconds = std::clamp( mSeconds, 0.01, 10.0 );
    if ( mRequest( "debug.trace.configure", { { "seconds", mSeconds } } ).at( "ok" ) == true )
    {
      mKeep( mSeconds );
    }
  }
  ImGui::SameLine();
  ImGui::Text( "(%d instructions)", total );
  ImGui::SameLine();
  ImGui::Checkbox( "Follow the newest", &mFollow );

  // The search, back from the newest, on the emulator's side.
  ImGui::Checkbox( "At", &mFindByPc );
  ImGui::SameLine();
  ImGui::BeginDisabled( !mFindByPc );
  inputAddress( "##findpc", mFindPc );
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetNextItemWidth( ImGui::GetFontSize() * 10.0F );
  ImGui::InputTextWithHint( "##findtext", "disassembly holds", mFindText.data(), mFindText.size() );
  ImGui::SameLine();
  if ( ImGui::Button( "Find" ) )
  {
    control::Json params{ { "limit", 200 } };
    if ( mFindByPc )
    {
      params["pc"] = addressOf( mFindPc );
    }
    if ( mFindText[0] != 0 )
    {
      params["disasm_contains"] = std::string{ mFindText.data() };
    }
    control::Json const response = mRequest( "debug.trace.find", std::move( params ) );
    mFound = resultOf( response );
    mLastError = errorOf( response );
  }
  if ( !mLastError.empty() )
  {
    ImGui::TextWrapped( "%s", mLastError.c_str() );
  }
  if ( !mFound.is_null() )
  {
    ImGui::SameLine();
    ImGui::Text(
        "%zu found%s", mFound.at( "matches" ).size(), mFound.at( "next_from" ).is_null() ? "" : ", more back" );
    if ( !mFound.at( "matches" ).empty() &&
         ImGui::BeginListBox( "##found", ImVec2{ -1.0F, ImGui::GetTextLineHeightWithSpacing() * 5.0F } ) )
    {
      for ( auto const& match : mFound.at( "matches" ) )
      {
        std::string const label = fmt::format( "{:>8}  {}  {}",
                                               match.at( "index" ).get<std::int64_t>(),
                                               match.at( "cs_ip" ).get<std::string>(),
                                               match.at( "disasm" ).get<std::string>() );
        if ( ImGui::Selectable( label.c_str() ) )
        {
          mShowIndex = match.at( "index" ).get<std::int64_t>();
          mFollow = false;
        }
      }
      ImGui::EndListBox();
    }
  }

  ImGuiTableFlags const flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
  if ( ImGui::BeginTable( "history", 8, flags ) )
  {
    ImGui::TableSetupScrollFreeze( 0, 1 );
    for ( char const* heading : { "Cycle",
                                  "CS:IP",
                                  "Bytes",
                                  "Instruction",
                                  "AX   BX   CX   DX",
                                  "SI   DI   BP   SP",
                                  "DS   ES   SS",
                                  "Flags" } )
    {
      ImGui::TableSetupColumn( heading );
    }
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin( total );
    while ( clipper.Step() )
    {
      // The rows on show, in one request. The table runs as the code ran, the
      // oldest at the top; the history is indexed back from the newest, and
      // answers newest first.
      int const shown = clipper.DisplayEnd - clipper.DisplayStart;
      control::Json const rows =
          resultOf( mRequest( "debug.trace", { { "from", total - clipper.DisplayEnd }, { "count", shown } } ) );
      control::Json const instructions = rows.is_null() ? control::Json::array() : rows.at( "instructions" );
      // The machine runs on between the count and the rows, and a power-up
      // or a shorter history may leave fewer than asked for: the rows the
      // clipper asked for are submitted all the same, those missing empty,
      // as it measures them.
      for ( auto missing = static_cast<int>( instructions.size() ); missing < shown; ++missing )
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( "" );
      }
      for ( auto entry = instructions.rbegin(); entry != instructions.rend(); ++entry )
      {
        control::Json const& row = *entry;
        auto const word = [&row]( char const* name ) { return row.at( name ).get<unsigned>(); };
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text( "%llu", static_cast<unsigned long long>( row.at( "cycle" ).get<std::uint64_t>() ) );
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( row.at( "cs_ip" ).get_ref<std::string const&>().c_str() );
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( row.at( "bytes_hex" ).get_ref<std::string const&>().c_str() );
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( row.at( "disasm" ).get_ref<std::string const&>().c_str() );
        ImGui::TableNextColumn();
        ImGui::Text( "%04X %04X %04X %04X", word( "ax" ), word( "bx" ), word( "cx" ), word( "dx" ) );
        ImGui::TableNextColumn();
        ImGui::Text( "%04X %04X %04X %04X", word( "si" ), word( "di" ), word( "bp" ), word( "sp" ) );
        ImGui::TableNextColumn();
        ImGui::Text( "%04X %04X %04X", word( "ds" ), word( "es" ), word( "ss" ) );
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( flagsText( word( "flags" ) ).c_str() );
      }
    }
    if ( mFollow )
    {
      ImGui::SetScrollHereY( 1.0F );
    }
    else if ( mShowIndex )
    {
      // Row 0 is the oldest; an index counts back from the newest. The index
      // found moves with every instruction run, so the row is that found
      // while the machine stands still.
      std::int64_t const row = std::max<std::int64_t>( 0, total - 1 - *mShowIndex );
      ImGui::SetScrollY( static_cast<float>( row ) * ImGui::GetTextLineHeightWithSpacing() );
      mShowIndex.reset();
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

IoWindow::IoWindow( Requester request ) : mRequest{ std::move( request ) } {}

void IoWindow::draw( bool& open )
{
  if ( !open )
  {
    return;
  }
  if ( !ImGui::Begin( "I/O", &open ) )
  {
    ImGui::End();
    return;
  }
  control::Json const io = resultOf( mRequest( "io.get_state", control::Json::object() ) );
  if ( io.is_null() )
  {
    ImGui::TextUnformatted( NO_MACHINE );
    ImGui::End();
    return;
  }
  for ( auto const& [device, registers] : io.items() )
  {
    if ( !registers.is_object() )
    {
      drawValue( device, registers );
    }
    else if ( ImGui::CollapsingHeader( device.c_str() ) )
    {
      for ( auto const& [name, value] : registers.items() )
      {
        drawValue( name, value );
      }
    }
  }
  ImGui::End();
}

} // namespace ws::app
