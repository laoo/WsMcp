#pragma once

// The methods of docs/spec/control-protocol.md, one group per file, and what
// they share for reading parameters. Private to the core: a transport sees only
// the Dispatcher.

#include "ws/Emulator.hpp"
#include "ws/control/Dispatcher.hpp"

#include <cstdint>
#include <expected>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ws::control
{

/// The bytes the cartridge has sent to the serial port and no client has read
/// yet: `serial.read` answers them, and `emu.run_until` looks into them. One
/// per dispatcher, shared by the methods that use it.
struct SerialLog
{
  std::vector<std::uint8_t> unread;
  /// The power-up the bytes came from: a new machine starts a new log.
  std::uint64_t powerUp{};

  /// Takes what the machine has sent since the last call into `unread`.
  void collect( Emulator& emulator );
};

void addEmuMethods( Dispatcher& dispatcher, Emulator& emulator );
void addCartMethods( Dispatcher& dispatcher, Emulator& emulator );
void addRunMethods( Dispatcher& dispatcher, Emulator& emulator, std::shared_ptr<SerialLog> const& serial );
void addMemoryMethods( Dispatcher& dispatcher, Emulator& emulator );
void addIoMethods( Dispatcher& dispatcher, Emulator& emulator );
void addCpuMethods( Dispatcher& dispatcher, Emulator& emulator );
void addVideoMethods( Dispatcher& dispatcher, Emulator& emulator );
void addInputMethods( Dispatcher& dispatcher, Emulator& emulator );
void addDebugMethods( Dispatcher& dispatcher, Emulator& emulator );
void addStateMethods( Dispatcher& dispatcher, Emulator& emulator );
void addAudioMethods( Dispatcher& dispatcher, Emulator& emulator );
void addSerialMethods( Dispatcher& dispatcher, Emulator& emulator, std::shared_ptr<SerialLog> const& serial );

Error badRequest( std::string message );

/// The `no_machine` error: why no machine is running.
Error noMachine( Emulator const& emulator );

/// The emulator's machine, or the `no_machine` error.
std::expected<machine::Machine*, Error> machineOf( Emulator& emulator );

/// What a run answers: why it stopped, and the cycles and frames it took.
Json runJson( machine::RunResult const& result );

/// Where a run stopped, when a breakpoint or a watchpoint stopped it: the
/// `breakpoint` or `watchpoint` object of its answer. Empty otherwise.
Json stopJson( machine::RunResult const& result );

/// One parameter of a method, as its schema describes it: a JSON Schema type
/// ("string", "integer", "boolean", "object", "array"), and what it means.
/// An address that may be written "SSSS:OOOO" is of type "integer or string",
/// given as the type ["integer", "string"].
struct Param
{
  std::string_view name;
  std::string_view type;
  std::string_view description;
  bool required{ true };
};

/// The JSON Schema of a method's `params`: an object of `params`.
Json paramsSchema( std::initializer_list<Param> params );

/// A method's description and the schema of its params.
MethodInfo info( std::string_view description, std::initializer_list<Param> params = {} );

/// The string `params[name]`, or the `bad_request` that says it is missing or
/// is not a string.
std::expected<std::string, Error> requireString( Json const& params, std::string_view name );

/// The non-negative integer `params[name]`, or the `bad_request` that says it
/// is missing or is not one. A string of hex digits after `0x` or `$` is one
/// too.
std::expected<std::uint64_t, Error> requireUnsigned( Json const& params, std::string_view name );

/// The schema type of a parameter that is an address: an integer, or a string
/// of hex digits or of a segment and an offset.
inline constexpr std::string_view ADDRESS_TYPE = "address";

/// An address in the V30MZ's 20-bit space, as a person writes one.
struct Address
{
  /// segment * 16 + offset, wrapped to 20 bits.
  std::uint32_t linear{};
  /// The segment and the offset, when it was written as "SSSS:OOOO".
  std::optional<std::uint16_t> segment;
  std::uint16_t offset{};
};

/// The linear address of `offset` in `segment`, wrapped to 20 bits as the bus
/// wraps it.
std::uint32_t linearOf( std::uint16_t segment, std::uint16_t offset );

/// An address written as an integer, 0 to $FFFFF; as hex digits after `0x` or
/// `$`; or as a segment and an offset of one to four hex digits each, either
/// with `$` or `0x` before it: "F000:FFF0", "$F000:$0100". Nothing for
/// anything else.
std::optional<Address> parseAddress( Json const& value );

/// The address `params[name]`, or the `bad_request` that says it is missing or
/// is not one.
std::expected<Address, Error> requireAddress( Json const& params, std::string_view name );

/// Bytes as two lowercase hex digits each.
std::string hexOf( std::span<std::uint8_t const> bytes );

/// Two hex digits a byte, of either case; nothing when `text` is not that.
std::optional<std::vector<std::uint8_t>> bytesOfHex( std::string_view text );

/// An address as "SSSS:OOOO", four uppercase hex digits each, as parseAddress
/// reads it.
std::string segmentedText( std::uint16_t segment, std::uint16_t offset );

/// The flags as a debugger shows them, ODITSZAPC, a capital for a set flag
/// and a small letter for a clear one.
std::string flagsText( std::uint16_t flags );

/// A CRC-32 as eight lowercase hex digits.
std::string crcText( std::uint32_t crc );

} // namespace ws::control
