#include "Methods.hpp"

#include <utility>

namespace ws::control
{

namespace
{

/// Bytes as text a JSON string can hold: printable ASCII, tab, line feed and
/// carriage return as themselves, any other byte as a dot.
std::string textOf( std::span<std::uint8_t const> bytes )
{
  std::string text;
  text.reserve( bytes.size() );
  for ( std::uint8_t const byte : bytes )
  {
    bool const plain = ( byte >= 0x20 && byte < 0x7f ) || byte == '\t' || byte == '\n' || byte == '\r';
    text.push_back( plain ? static_cast<char>( byte ) : '.' );
  }
  return text;
}

} // namespace

void SerialLog::collect( Emulator& emulator )
{
  machine::Machine* const machine = emulator.machine();
  if ( machine == nullptr )
  {
    return;
  }
  if ( emulator.powerUps() != powerUp )
  {
    unread.clear();
    powerUp = emulator.powerUps();
  }
  std::vector<std::uint8_t> const sent = machine->takeSerialOutput();
  unread.insert( unread.end(), sent.begin(), sent.end() );
}

void addSerialMethods( Dispatcher& dispatcher, Emulator& emulator, std::shared_ptr<SerialLog> const& serial )
{
  dispatcher.add( "serial.read",
                  info( "The bytes the cartridge has sent out of the serial port (the EXT port's UART, 9600 or 38400 "
                        "baud) since the last serial.read, or since power-up: as hex, and as text with every byte "
                        "but printable ASCII, tab, line feed and carriage return shown as a dot. Nothing is "
                        "connected to the port: what is sent is kept, and nothing is received." ),
                  [&emulator, serial]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    serial->collect( emulator );
                    std::vector<std::uint8_t> const bytes = std::exchange( serial->unread, {} );
                    return Json{ { "size", bytes.size() }, { "hex", hexOf( bytes ) }, { "text", textOf( bytes ) } };
                  } );
}

} // namespace ws::control
