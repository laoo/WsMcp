#include <catch2/catch_test_macros.hpp>

#include "cpu/Disassembler.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

// The CPU reaches memory only through a whole System, so what is held here is
// what stands without one: the disassembler, against encodings worked out by
// hand from the V30MZ's opcode map, in Mesen2's syntax.

namespace
{

ws::machine::Instruction disassembleAt( std::uint16_t ip, std::initializer_list<std::uint8_t> bytes )
{
  std::vector<std::uint8_t> const code( bytes );
  return ws::machine::disassemble( code, ip );
}

void requireText( std::initializer_list<std::uint8_t> bytes, std::string const& text, std::uint8_t size )
{
  auto const instruction = disassembleAt( 0x0100, bytes );
  INFO( text );
  REQUIRE( instruction.text == text );
  REQUIRE( instruction.size == size );
  REQUIRE( ws::machine::getOpSize( std::vector<std::uint8_t>( bytes ) ) == size );
}

} // namespace

TEST_CASE( "immediates and register operands", "[cpu][disassembler]" )
{
  requireText( { 0xB8, 0x34, 0x12 }, "MOV AX, $1234", 3 );
  requireText( { 0xB4, 0x7F }, "MOV AH, $7F", 2 );
  requireText( { 0x89, 0xD8 }, "MOV AX, BX", 2 );
  requireText( { 0x8A, 0xC4 }, "MOV AL, AH", 2 );
  requireText( { 0xCD, 0x21 }, "INT $21", 2 );
  requireText( { 0xE4, 0xC0 }, "IN AL, $C0", 2 );
  requireText( { 0xE6, 0xB6 }, "OUT $B6, AL", 2 );
  requireText( { 0xC8, 0x10, 0x00, 0x01 }, "ENTER $0010, $01", 4 );
  requireText( { 0x90 }, "NOP", 1 );
  requireText( { 0xD8, 0x00 }, "UNDEFINED_D8", 1 );
}

TEST_CASE( "memory operands take their segment and displacement", "[cpu][disassembler]" )
{
  requireText( { 0x8B, 0x46, 0xFE }, "MOV AX, SS:[BP-$02]", 3 );
  requireText( { 0x8B, 0x87, 0x00, 0x10 }, "MOV AX, DS:[BX+$1000]", 4 );
  requireText( { 0x8B, 0x86, 0x00, 0xF0 }, "MOV AX, SS:[BP-$1000]", 4 );
  requireText( { 0xA1, 0x00, 0x20 }, "MOV AX, DS:[$2000]", 3 );
  requireText( { 0x80, 0x3E, 0x34, 0x12, 0x05 }, "CMP DS:[$1234], $05", 5 );
  requireText( { 0xC7, 0x06, 0x00, 0x20, 0x34, 0x12 }, "MOV DS:[$2000], $1234", 6 );
}

TEST_CASE( "a segment prefix replaces the default segment and is not written", "[cpu][disassembler]" )
{
  requireText( { 0x26, 0x8A, 0x07 }, "MOV AL, ES:[BX]", 3 );
  requireText( { 0x2E, 0xA1, 0x00, 0x20 }, "MOV AX, CS:[$2000]", 4 );
  requireText( { 0x3E, 0x8B, 0x46, 0x02 }, "MOV AX, DS:[BP+$02]", 4 );
}

TEST_CASE( "REP and LOCK are written before the instruction", "[cpu][disassembler]" )
{
  requireText( { 0xF3, 0xA4 }, "REPZ MOVSB", 2 );
  requireText( { 0x36, 0xF2, 0xA6 }, "REPNZ CMPSB", 3 );
  requireText( { 0xF0, 0x90 }, "LOCK NOP", 2 );
}

TEST_CASE( "the groups take their operation from the ModR/M byte", "[cpu][disassembler]" )
{
  requireText( { 0x81, 0xC3, 0x34, 0x12 }, "ADD BX, $1234", 4 );
  requireText( { 0x83, 0xEC, 0x02 }, "SUB SP, $0002", 3 );
  requireText( { 0x83, 0xC0, 0xFF }, "ADD AX, $FFFF", 3 );
  requireText( { 0xC1, 0xE0, 0x04 }, "SHL AX, $04", 3 );
  requireText( { 0xD3, 0xE8 }, "SHR AX, CL", 2 );
  requireText( { 0xD0, 0xF0 }, "UNDEFINED_GRP2_6 AL", 2 );
  requireText( { 0xF7, 0x06, 0x34, 0x12, 0xCD, 0xAB }, "TEST DS:[$1234], $ABCD", 6 );
  requireText( { 0xF7, 0xE3 }, "MUL AX, BX", 2 );
  requireText( { 0xF6, 0xF1 }, "DIV CL", 2 );
  requireText( { 0xFF, 0xE0 }, "JMP AX", 2 );
  requireText( { 0xFF, 0x1E, 0x34, 0x12 }, "CALL FAR DS:[$1234]", 4 );
  requireText( { 0xFE, 0x06, 0x00, 0x20 }, "INC DS:[$2000]", 4 );
}

TEST_CASE( "LEA, LDS and LES read register ModR/M as an address", "[cpu][disassembler]" )
{
  requireText( { 0x8D, 0x40, 0x10 }, "LEA AX, DS:[BX+SI+$10]", 3 );
  requireText( { 0x8D, 0xC0 }, "LEA AX, DS:[BX+AX]", 2 );
  requireText( { 0xC4, 0xD6 }, "LES DX, SS:[BP+SI]", 2 );
}

TEST_CASE( "segment register moves", "[cpu][disassembler]" )
{
  requireText( { 0x8E, 0xD8 }, "MOV DS, AX", 2 );
  requireText( { 0x8C, 0xC8 }, "MOV AX, CS", 2 );
}

TEST_CASE( "a ModR/M alone with an immediate names a word register by the opcode's width", "[cpu][disassembler]" )
{
  // Mesen2 writes these two as AL and AL: see 'n' in Disassembler.cpp.
  requireText( { 0xC7, 0xC0, 0x34, 0x12 }, "MOV AX, $1234", 4 );
  requireText( { 0x8F, 0xC3 }, "POP BX", 2 );
  requireText( { 0xC6, 0xC0, 0x12 }, "MOV AL, $12", 3 );
  // Mesen2's IMUL shows the source and the immediate alone.
  requireText( { 0x69, 0xC3, 0x10, 0x00 }, "IMUL BX, $0010", 4 );
}

TEST_CASE( "relative jumps are shown as the offset they land at", "[cpu][disassembler]" )
{
  requireText( { 0x74, 0x05 }, "JZ $0107", 2 );
  requireText( { 0x75, 0xFE }, "JNZ $0100", 2 );
  requireText( { 0xE8, 0xFD, 0xFF }, "CALL $0100", 3 );
  requireText( { 0xE9, 0x00, 0x10 }, "JMP $1103", 3 );
  requireText( { 0xE2, 0xF0 }, "LOOP $00F2", 2 );

  // At the top of the segment the destination wraps.
  REQUIRE( disassembleAt( 0xFFFE, { 0xEB, 0x10 } ).text == "JMP $0010" );
}

TEST_CASE( "far jumps and calls are shown as segment and offset", "[cpu][disassembler]" )
{
  requireText( { 0xEA, 0x00, 0x01, 0x00, 0xF0 }, "JMP $F000:$0100", 5 );
  requireText( { 0x9A, 0x34, 0x12, 0x00, 0x20 }, "CALL $2000:$1234", 5 );
}

TEST_CASE( "bytes past the end read as zero", "[cpu][disassembler]" )
{
  auto const instruction = disassembleAt( 0x0100, { 0xB8 } );
  REQUIRE( instruction.text == "MOV AX, $0000" );
  REQUIRE( instruction.size == 3 );
  REQUIRE( ws::machine::disassemble( {}, 0 ).text == "ADD DS:[BX+SI], AL" );
}

TEST_CASE( "an opcode's kind, past its prefixes", "[cpu][disassembler]" )
{
  using namespace ws::machine;

  std::vector<std::uint8_t> const callFar{ 0x26, 0xFF, 0x1E, 0x34, 0x12 };
  REQUIRE( getFullOpCode( callFar ) == 0x1EFF );
  REQUIRE( isJumpToSub( getFullOpCode( callFar ) ) );
  REQUIRE( isUnconditionalJump( getFullOpCode( callFar ) ) );

  REQUIRE( isJumpToSub( 0x00E8 ) );
  REQUIRE_FALSE( isJumpToSub( 0x00E9 ) );
  REQUIRE( isUnconditionalJump( 0x00EB ) );
  REQUIRE( isConditionalJump( 0x0074 ) );
  REQUIRE( isConditionalJump( 0x00E3 ) );
  REQUIRE_FALSE( isConditionalJump( 0x00EB ) );
  REQUIRE( isReturnInstruction( 0x00CF ) );
  REQUIRE( isUndefinedOpCode( 0x000F ) );
  REQUIRE( isUndefinedOpCode( 0x30D0 ) );
  REQUIRE_FALSE( isUndefinedOpCode( 0x20D0 ) );
  REQUIRE( isPushPopInstruction( 0x009C ) );
  REQUIRE( isPrefix( 0x3E ) );
  REQUIRE_FALSE( isPrefix( 0x90 ) );
}
