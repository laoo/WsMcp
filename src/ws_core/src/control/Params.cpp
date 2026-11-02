#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <charconv>
#include <optional>
#include <system_error>
#include <utility>

namespace ws::control
{

namespace
{

constexpr std::uint32_t ADDRESS_SPACE = 0x100000;

/// The value of hex digits, all of `text`; nothing when it is empty, holds
/// anything else, or is longer than `maxDigits`.
std::optional<std::uint64_t> parseDigits( std::string_view text, std::size_t maxDigits )
{
  std::uint64_t value = 0;
  auto const [end, error] = std::from_chars( text.data(), text.data() + text.size(), value, 16 );
  if ( text.empty() || text.size() > maxDigits || error != std::errc{} || end != text.data() + text.size() )
  {
    return std::nullopt;
  }
  return value;
}

/// `text` without the `0x` or `$` before it, and whether there was one.
std::pair<std::string_view, bool> withoutHexPrefix( std::string_view text )
{
  if ( text.starts_with( "0x" ) || text.starts_with( "0X" ) )
  {
    return { text.substr( 2 ), true };
  }
  if ( text.starts_with( '$' ) )
  {
    return { text.substr( 1 ), true };
  }
  return { text, false };
}

/// The value of hex digits after `0x` or `$`, as a person writes an address.
std::optional<std::uint64_t> parseHex( std::string_view text )
{
  auto const [digits, prefixed] = withoutHexPrefix( text );
  if ( !prefixed )
  {
    return std::nullopt;
  }
  return parseDigits( digits, 16 );
}

/// A segment or an offset of "SSSS:OOOO": up to four hex digits, `$` or `0x`
/// before them or not.
std::optional<std::uint16_t> parseWord( std::string_view text )
{
  auto const value = parseDigits( withoutHexPrefix( text ).first, 4 );
  if ( !value )
  {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>( *value );
}

int hexDigit( char c )
{
  if ( c >= '0' && c <= '9' )
  {
    return c - '0';
  }
  if ( c >= 'a' && c <= 'f' )
  {
    return c - 'a' + 10;
  }
  if ( c >= 'A' && c <= 'F' )
  {
    return c - 'A' + 10;
  }
  return -1;
}

} // namespace

Error badRequest( std::string message )
{
  return Error{ .code = "bad_request", .message = std::move( message ) };
}

Error noMachine( Emulator const& emulator )
{
  return Error{ .code = "no_machine", .message = fmt::format( "No machine is running: {}", emulator.powerUpError() ) };
}

std::expected<machine::Machine*, Error> machineOf( Emulator& emulator )
{
  if ( machine::Machine* const machine = emulator.machine() )
  {
    return machine;
  }
  return std::unexpected( noMachine( emulator ) );
}

std::expected<std::string, Error> requireString( Json const& params, std::string_view name )
{
  auto const field = params.find( name );
  if ( field == params.end() || !field->is_string() )
  {
    return std::unexpected( badRequest( fmt::format( "Missing or invalid field: {}", name ) ) );
  }
  return field->get<std::string>();
}

std::expected<std::uint64_t, Error> requireUnsigned( Json const& params, std::string_view name )
{
  // Parsed text holds a non-negative integer as unsigned, a request built in
  // C++ from an `int` holds it as signed; the value is what is checked.
  auto const field = params.find( name );
  if ( field != params.end() && field->is_string() )
  {
    if ( auto const value = parseHex( field->get_ref<std::string const&>() ) )
    {
      return *value;
    }
  }
  else if ( field != params.end() && field->is_number_integer() &&
            ( field->is_number_unsigned() || field->get<std::int64_t>() >= 0 ) )
  {
    return field->get<std::uint64_t>();
  }
  return std::unexpected( badRequest( fmt::format( "Missing or invalid field: {}", name ) ) );
}

std::uint32_t linearOf( std::uint16_t segment, std::uint16_t offset )
{
  return ( ( static_cast<std::uint32_t>( segment ) << 4U ) + offset ) & ( ADDRESS_SPACE - 1 );
}

std::optional<Address> parseAddress( Json const& value )
{
  std::optional<std::uint64_t> linear;
  if ( value.is_number_integer() && ( value.is_number_unsigned() || value.get<std::int64_t>() >= 0 ) )
  {
    linear = value.get<std::uint64_t>();
  }
  else if ( value.is_string() )
  {
    auto const& text = value.get_ref<std::string const&>();
    if ( auto const colon = text.find( ':' ); colon != std::string::npos )
    {
      auto const segment = parseWord( std::string_view{ text }.substr( 0, colon ) );
      auto const offset = parseWord( std::string_view{ text }.substr( colon + 1 ) );
      if ( !segment || !offset )
      {
        return std::nullopt;
      }
      return Address{ .linear = linearOf( *segment, *offset ), .segment = segment, .offset = *offset };
    }
    linear = parseHex( text );
  }
  if ( !linear || *linear >= ADDRESS_SPACE )
  {
    return std::nullopt;
  }
  return Address{ .linear = static_cast<std::uint32_t>( *linear ),
                  .segment = std::nullopt,
                  .offset = static_cast<std::uint16_t>( *linear ) };
}

std::expected<Address, Error> requireAddress( Json const& params, std::string_view name )
{
  auto const field = params.find( name );
  if ( field != params.end() )
  {
    if ( auto const address = parseAddress( *field ) )
    {
      return *address;
    }
  }
  return std::unexpected( badRequest( fmt::format(
      "Missing or invalid field: {}, an address: 0 to $FFFFF, or a segment and offset, \"F000:FFF0\"", name ) ) );
}

std::string hexOf( std::span<std::uint8_t const> bytes )
{
  static constexpr std::string_view DIGITS = "0123456789abcdef";
  std::string text;
  text.reserve( bytes.size() * 2 );
  for ( std::uint8_t const byte : bytes )
  {
    text.push_back( DIGITS[byte >> 4U] );
    text.push_back( DIGITS[byte & 0x0fU] );
  }
  return text;
}

std::optional<std::vector<std::uint8_t>> bytesOfHex( std::string_view text )
{
  if ( text.size() % 2 != 0 )
  {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve( text.size() / 2 );
  for ( std::size_t at = 0; at < text.size(); at += 2 )
  {
    int const high = hexDigit( text[at] );
    int const low = hexDigit( text[at + 1] );
    if ( high < 0 || low < 0 )
    {
      return std::nullopt;
    }
    bytes.push_back( static_cast<std::uint8_t>( ( high << 4 ) | low ) );
  }
  return bytes;
}

std::string crcText( std::uint32_t crc )
{
  return fmt::format( "{:08x}", crc );
}

Json paramsSchema( std::initializer_list<Param> params )
{
  Json properties = Json::object();
  Json required = Json::array();
  for ( Param const& param : params )
  {
    Json property;
    if ( param.type == ADDRESS_TYPE )
    {
      property["type"] = Json::array( { "integer", "string" } );
    }
    else
    {
      property["type"] = param.type;
    }
    // Every array a method takes is of names.
    if ( param.type == "array" )
    {
      property["items"] = Json{ { "type", "string" } };
    }
    property["description"] = param.description;
    properties[std::string{ param.name }] = std::move( property );
    if ( param.required )
    {
      required.push_back( param.name );
    }
  }
  Json schema{ { "type", "object" }, { "properties", std::move( properties ) } };
  if ( !required.empty() )
  {
    schema["required"] = std::move( required );
  }
  return schema;
}

MethodInfo info( std::string_view description, std::initializer_list<Param> params )
{
  return MethodInfo{ .description = std::string{ description }, .params = paramsSchema( params ) };
}

} // namespace ws::control
