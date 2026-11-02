// The V30MZ's disassembler, and what a debugger asks of an opcode. Ported from
// Mesen2's Core/WS/Debugger/WsDisUtils.h and WsDisUtils.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "cpu/Disassembler.hpp"

#include "ws/machine/Types.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace ws::machine
{

namespace
{

constexpr std::size_t MAX_BYTES = 8;

constexpr std::array<char const*, 8> MOD_REG_LUT8 = { "AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH" };
constexpr std::array<char const*, 8> MOD_REG_LUT16 = { "AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI" };
constexpr std::array<char const*, 4> MOD_SEG_REG_LUT = { "ES", "CS", "SS", "DS" };

// Each letter is an operand: a = [address], i / j = byte / word immediate,
// m = ModR/M with a register, n = ModR/M alone, o = ModR/M of LEA/LDS/LES,
// p = ModR/M of BOUND, q = ModR/M with a segment register, r / s = byte / word
// relative jump, t = far address, v / w / x / y / z = groups 1 / 2 / 3 / 4 / 5.
// clang-format off
constexpr std::array<std::string_view, 256> OP_TEMPLATE = {
  // 0-0F
  "ADD m", "ADD m", "ADD m", "ADD m", "ADD AL, i", "ADD AX, j", "PUSH ES", "POP ES",
  "OR m",  "OR m",  "OR m",  "OR m",  "OR AL, i",  "OR AX, j",  "PUSH CS", "UNDEFINED_0F",

  // 10-1F
  "ADC m", "ADC m", "ADC m", "ADC m", "ADC AL, i", "ADC AX, j", "PUSH SS", "POP SS",
  "SBB m", "SBB m", "SBB m", "SBB m", "SBB AL, i", "SBB AX, j", "PUSH DS", "POP DS",

  // 20-2F
  "AND m", "AND m", "AND m", "AND m", "AND AL, i", "AND AX, j", "ES:", "DAA",
  "SUB m", "SUB m", "SUB m", "SUB m", "SUB AL, i", "SUB AX, j", "CS:", "DAS",

  // 30-3F
  "XOR m", "XOR m", "XOR m", "XOR m", "XOR AL, i", "XOR AX, j", "SS:", "AAA",
  "CMP m", "CMP m", "CMP m", "CMP m", "CMP AL, i", "CMP AX, j", "DS:", "AAS",

  // 40-4F
  "INC AX", "INC CX", "INC DX", "INC BX", "INC SP", "INC BP", "INC SI", "INC DI",
  "DEC AX", "DEC CX", "DEC DX", "DEC BX", "DEC SP", "DEC BP", "DEC SI", "DEC DI",

  // 50-5F
  "PUSH AX", "PUSH CX", "PUSH DX", "PUSH BX", "PUSH SP", "PUSH BP", "PUSH SI", "PUSH DI",
  "POP AX", "POP CX", "POP DX", "POP BX", "POP SP", "POP BP", "POP SI", "POP DI",

  // 60-6F
  "PUSHA", "POPA", "BOUND p", "UNDEFINED_63", "UNDEFINED_64", "UNDEFINED_65", "UNDEFINED_66", "UNDEFINED_67",
  "PUSH j", "IMUL n, j", "PUSH i", "IMUL n, i", "INSB", "INSW", "OUTSB", "OUTSW",

  // 70-7F
  "JO r", "JNO r", "JB r", "JNB r", "JZ r", "JNZ r", "JBE r", "JA r",
  "JS r", "JNS r", "JPE r", "JPO r", "JL r", "JGE r", "JLE r", "JG r",

  // 80-8F
  "v", "v", "v", "v", "TEST m", "TEST m", "XCHG m", "XCHG m",
  "MOV m", "MOV m", "MOV m", "MOV m", "MOV q", "LEA o", "MOV q", "POP n",

  // 90-9F
  "NOP", "XCHG CX, AX", "XCHG DX, AX", "XCHG BX, AX", "XCHG SP, AX", "XCHG BP, AX", "XCHG SI, AX", "XCHG DI, AX",
  "CBW", "CWD", "CALL t", "WAIT", "PUSHF", "POPF", "SAHF", "LAHF",

  // A0-AF
  "MOV AL, a", "MOV AX, a", "MOV a, AL", "MOV a, AX", "MOVSB", "MOVSW", "CMPSB", "CMPSW",
  "TEST AL, i", "TEST AX, j", "STOSB", "STOSW", "LODSB", "LODSW", "SCASB", "SCASW",

  // B0-BF
  "MOV AL, i", "MOV CL, i", "MOV DL, i", "MOV BL, i", "MOV AH, i", "MOV CH, i", "MOV DH, i", "MOV BH, i",
  "MOV AX, j", "MOV CX, j", "MOV DX, j", "MOV BX, j", "MOV SP, j", "MOV BP, j", "MOV SI, j", "MOV DI, j",

  // C0-CF
  "w, i", "w, i", "RET j", "RET", "LES o", "LDS o", "MOV n, i", "MOV n, j",
  "ENTER j, i", "LEAVE", "RETF j", "RETF", "INT 3", "INT i" , "INTO", "IRET",

  // D0-DF
  "w", "w", "w, CL", "w, CL", "AAM i", "AAD i", "SALC", "XLAT",
  "UNDEFINED_D8", "UNDEFINED_D9", "UNDEFINED_DA", "UNDEFINED_DB", "UNDEFINED_DC", "UNDEFINED_DD", "UNDEFINED_DE", "UNDEFINED_DF",

  // E0-EF
  "LOOPNZ r", "LOOPZ r", "LOOP r", "JCXZ r", "IN AL, i", "IN AX, i", "OUT i, AL", "OUT i, AX",
  "CALL s", "JMP s", "JMP t", "JMP r", "IN AL, DX", "IN AX, DX", "OUT DX, AL", "OUT DX, AX",

  // F0-FF
  "LOCK", "UNDEFINED_F1", "REPNZ", "REPZ", "HLT", "CMC", "x", "x",
  "CLC", "STC", "CLI", "STI", "CLD", "STD", "y", "z"
};
// clang-format on

std::uint8_t byteAt( std::span<std::uint8_t const> bytes, std::size_t at )
{
  return at < bytes.size() && at < MAX_BYTES ? bytes[at] : 0;
}

std::uint16_t wordAt( std::span<std::uint8_t const> bytes, std::size_t at )
{
  return static_cast<std::uint16_t>( byteAt( bytes, at ) | ( byteAt( bytes, at + 1 ) << 8U ) );
}

void writeHex( std::string& str, std::uint8_t value )
{
  constexpr std::string_view digits = "0123456789ABCDEF";
  str += digits[value >> 4U];
  str += digits[value & 0x0FU];
}

void writeHex( std::string& str, std::uint16_t value )
{
  writeHex( str, static_cast<std::uint8_t>( value >> 8U ) );
  writeHex( str, static_cast<std::uint8_t>( value ) );
}

/// A ModR/M displacement, signed, as Mesen2's GetJmpDestination writes it.
void writeDisplacement( std::string& str, std::span<std::uint8_t const> bytes, std::size_t at, std::uint8_t size )
{
  if ( size == 2 )
  {
    auto const offset = static_cast<std::int16_t>( wordAt( bytes, at ) );
    if ( offset < 0 )
    {
      str += "-$";
      writeHex( str, static_cast<std::uint16_t>( -offset ) );
    }
    else
    {
      str += "+$";
      writeHex( str, static_cast<std::uint16_t>( offset ) );
    }
  }
  else
  {
    auto const offset = static_cast<std::int8_t>( byteAt( bytes, at ) );
    if ( offset < 0 )
    {
      str += "-$";
      writeHex( str, static_cast<std::uint8_t>( -offset ) );
    }
    else
    {
      str += "+$";
      writeHex( str, static_cast<std::uint8_t>( offset ) );
    }
  }
}

/// A jump's destination: Mesen2 writes a relative jump's displacement,
/// `+$05`; here it is the offset it lands at, `$0107`.
void getJmpDestination(
    std::string& str, std::span<std::uint8_t const> bytes, std::size_t at, std::uint8_t size, std::uint16_t nextIp )
{
  if ( size == 4 )
  {
    str += '$';
    writeHex( str, wordAt( bytes, at + 2 ) );
    str += ":$";
    writeHex( str, wordAt( bytes, at ) );
  }
  else
  {
    int const offset =
        size == 2 ? static_cast<std::int16_t>( wordAt( bytes, at ) ) : static_cast<std::int8_t>( byteAt( bytes, at ) );
    str += '$';
    writeHex( str, static_cast<std::uint16_t>( nextIp + offset ) );
  }
}

void getSegment( std::string& str, Segment segment, std::string_view defaultSegment )
{
  switch ( segment )
  {
  default:
  case Segment::DEFAULT:
    str += defaultSegment;
    str += ':';
    break;
  case Segment::ES:
    str += "ES:";
    break;
  case Segment::SS:
    str += "SS:";
    break;
  case Segment::CS:
    str += "CS:";
    break;
  case Segment::DS:
    str += "DS:";
    break;
  }
}

void getModRegParam( std::string& str, std::span<std::uint8_t const> bytes, std::size_t at, bool word )
{
  std::uint8_t const modRm = byteAt( bytes, at + 1 );
  std::uint8_t const reg = ( modRm >> 3U ) & 0x07U;
  str += word ? MOD_REG_LUT16[reg] : MOD_REG_LUT8[reg];
}

void getModSegRegParam( std::string& str, std::span<std::uint8_t const> bytes, std::size_t at )
{
  std::uint8_t const modRm = byteAt( bytes, at + 1 );
  std::uint8_t const reg = ( modRm >> 3U ) & 0x03U;
  str += MOD_SEG_REG_LUT[reg];
}

/// Writes the ModR/M operand of the instruction whose opcode is at `at`, and
/// answers how far it reaches past the opcode.
std::size_t getModRmParam( std::string& str,
                           std::span<std::uint8_t const> bytes,
                           std::size_t at,
                           Segment segment,
                           bool word,
                           bool forLeaLdsLes = false )
{
  std::uint8_t const modRm = byteAt( bytes, at + 1 );
  std::uint8_t const mode = ( modRm >> 6U ) & 0x03U;
  std::uint8_t const rm = modRm & 0x07U;
  std::size_t len = 1;

  if ( mode == 3 )
  {
    if ( forLeaLdsLes )
    {
      switch ( rm )
      {
      case 0x00:
        getSegment( str, segment, "DS" );
        str += "[BX+AX]";
        break;
      case 0x01:
        getSegment( str, segment, "DS" );
        str += "[BX+CX]";
        break;
      case 0x02:
        getSegment( str, segment, "SS" );
        str += "[BP+DX]";
        break;
      case 0x03:
        getSegment( str, segment, "SS" );
        str += "[BP+BX]";
        break;
      case 0x04:
        getSegment( str, segment, "DS" );
        str += "[SP+SI]";
        break;
      case 0x05:
        getSegment( str, segment, "DS" );
        str += "[BP+DI]";
        break;
      case 0x06:
        getSegment( str, segment, "SS" );
        str += "[BP+SI]";
        break;
      default:
        getSegment( str, segment, "DS" );
        str += "[BX+DI]";
        break;
      }
    }
    else
    {
      str += word ? MOD_REG_LUT16[rm] : MOD_REG_LUT8[rm];
    }
  }
  else if ( mode == 0 && rm == 0x06 )
  {
    getSegment( str, segment, "DS" );
    str += "[$";
    writeHex( str, wordAt( bytes, at + 2 ) );
    str += ']';
    len += 2;
  }
  else
  {
    switch ( rm )
    {
    case 0x00:
      getSegment( str, segment, "DS" );
      str += "[BX+SI";
      break;
    case 0x01:
      getSegment( str, segment, "DS" );
      str += "[BX+DI";
      break;
    case 0x02:
      getSegment( str, segment, "SS" );
      str += "[BP+SI";
      break;
    case 0x03:
      getSegment( str, segment, "SS" );
      str += "[BP+DI";
      break;
    case 0x04:
      getSegment( str, segment, "DS" );
      str += "[SI";
      break;
    case 0x05:
      getSegment( str, segment, "DS" );
      str += "[DI";
      break;
    case 0x06:
      getSegment( str, segment, "SS" );
      str += "[BP";
      break;
    default:
      getSegment( str, segment, "DS" );
      str += "[BX";
      break;
    }

    if ( mode == 1 )
    {
      writeDisplacement( str, bytes, at + 2, 1 );
      str += ']';
      len++;
    }
    else if ( mode == 2 )
    {
      writeDisplacement( str, bytes, at + 2, 2 );
      str += ']';
      len += 2;
    }
    else
    {
      str += ']';
    }
  }

  return len;
}

std::uint8_t getModRmSize( std::uint8_t modRm )
{
  std::uint8_t const mode = ( modRm >> 6U ) & 0x03U;
  std::uint8_t const rm = modRm & 0x07U;
  if ( mode == 2 || ( mode == 0 && rm == 0x06 ) )
  {
    return 3;
  }
  if ( mode == 1 )
  {
    return 2;
  }
  return 1;
}

} // namespace

bool isPrefix( std::uint8_t opCode )
{
  return opCode == 0xF0 || opCode == 0xF2 || opCode == 0xF3 || opCode == 0x26 || opCode == 0x36 || opCode == 0x2E ||
         opCode == 0x3E;
}

Instruction disassemble( std::span<std::uint8_t const> bytes, std::uint16_t ip )
{
  Instruction instruction;
  instruction.size = getOpSize( bytes );
  std::string& str = instruction.text;
  auto const nextIp = static_cast<std::uint16_t>( ip + instruction.size );

  std::size_t at = 0;
  std::uint8_t opCode = byteAt( bytes, at );

  Segment segment = Segment::DEFAULT;
  while ( isPrefix( opCode ) )
  {
    switch ( opCode )
    {
    case 0x26:
      segment = Segment::ES;
      break;
    case 0x36:
      segment = Segment::SS;
      break;
    case 0x2E:
      segment = Segment::CS;
      break;
    case 0x3E:
      segment = Segment::DS;
      break;

    default:
      // rep, lock
      str += OP_TEMPLATE[opCode];
      str += ' ';
      break;
    }

    at++;
    opCode = byteAt( bytes, at );
  }

  for ( char const c : OP_TEMPLATE[opCode] )
  {
    switch ( c )
    {
    // Address offset
    case 'a':
      getSegment( str, segment, "DS" );
      str += "[$";
      writeHex( str, wordAt( bytes, at + 1 ) );
      str += ']';
      break;

    // Immediate values
    case 'i':
      str += '$';
      writeHex( str, byteAt( bytes, at + 1 ) );
      at++;
      break;
    case 'j':
      str += '$';
      writeHex( str, wordAt( bytes, at + 1 ) );
      at += 2;
      break;

    // ModRM (2 operands)
    case 'm':
    {
      bool const word = ( opCode & 0x01U ) != 0;
      bool const direction = ( opCode & 0x02U ) != 0;

      if ( direction )
      {
        getModRegParam( str, bytes, at, word );
        str += ", ";
        getModRmParam( str, bytes, at, segment, word );
      }
      else
      {
        getModRmParam( str, bytes, at, segment, word );
        str += ", ";
        getModRegParam( str, bytes, at, word );
      }
      break;
    }

    // ModRM (+ immediate value)
    case 'n':
    {
      // Mesen2 tests the template's letter for an 'l' it never has, so it
      // always writes a register operand as a byte register; the opcode's
      // width bit says which it is.
      bool const word = ( opCode & 0x01U ) != 0;
      at += getModRmParam( str, bytes, at, segment, word );
      break;
    }

    // ModRM (2 operands, LEA/LDS/LES)
    case 'o':
      getModRegParam( str, bytes, at, true );
      str += ", ";
      getModRmParam( str, bytes, at, segment, true, true );
      break;

    // ModRM (2 operands, BOUND)
    case 'p':
      getModRegParam( str, bytes, at, true );
      str += ", ";
      getModRmParam( str, bytes, at, segment, true, false );
      break;

    // ModRM (mov segment)
    case 'q':
    {
      bool const direction = ( opCode & 0x02U ) != 0;
      if ( direction )
      {
        getModSegRegParam( str, bytes, at );
        str += ", ";
        getModRmParam( str, bytes, at, segment, true );
      }
      else
      {
        getModRmParam( str, bytes, at, segment, true );
        str += ", ";
        getModSegRegParam( str, bytes, at );
      }
      break;
    }

    // Relative jumps
    case 'r':
      getJmpDestination( str, bytes, at + 1, 1, nextIp );
      break;
    case 's':
      getJmpDestination( str, bytes, at + 1, 2, nextIp );
      break;
    case 't':
      getJmpDestination( str, bytes, at + 1, 4, nextIp );
      break;

    case 'v': // GRP1
    {
      static constexpr std::array<std::string_view, 8> NAMES = { "ADD ", "OR ",  "ADC ", "SBB ",
                                                                 "AND ", "SUB ", "XOR ", "CMP " };
      str += NAMES[( byteAt( bytes, at + 1 ) >> 3U ) & 0x07U];

      at += getModRmParam( str, bytes, at, segment, ( opCode & 0x01U ) != 0 );

      str += ", $";
      if ( ( opCode & 0x03U ) == 0x01 )
      {
        writeHex( str, wordAt( bytes, at + 1 ) );
      }
      else if ( ( opCode & 0x03U ) == 0x03 )
      {
        writeHex( str, static_cast<std::uint16_t>( static_cast<std::int8_t>( byteAt( bytes, at + 1 ) ) ) );
      }
      else
      {
        writeHex( str, byteAt( bytes, at + 1 ) );
      }
      break;
    }

    case 'w': // GRP2
    {
      static constexpr std::array<std::string_view, 8> NAMES = {
        "ROL ", "ROR ", "RCL ", "RCR ", "SHL ", "SHR ", "UNDEFINED_GRP2_6 ", "SAR "
      };
      str += NAMES[( byteAt( bytes, at + 1 ) >> 3U ) & 0x07U];

      // Followed by an immediate value for C0/C1
      at += getModRmParam( str, bytes, at, segment, ( opCode & 0x01U ) != 0 );
      break;
    }

    case 'x': // GRP3
    {
      std::uint8_t const mode = ( byteAt( bytes, at + 1 ) >> 3U ) & 0x07U;
      bool const word = ( opCode & 0x01U ) != 0;
      switch ( mode )
      {
      case 0:
        str += "TEST ";
        break;
      case 1:
        str += "UNDEFINED_GRP3_1 ";
        break;
      case 2:
        str += "NOT ";
        break;
      case 3:
        str += "NEG ";
        break;
      case 4:
        str += word ? "MUL AX, " : "MUL ";
        break;
      case 5:
        str += word ? "IMUL AX, " : "IMUL ";
        break;
      case 6:
        str += word ? "DIV AX, " : "DIV ";
        break;
      default:
        str += word ? "IDIV AX, " : "IDIV ";
        break;
      }
      at += getModRmParam( str, bytes, at, segment, word );
      if ( mode == 0 )
      {
        str += ", $";
        if ( word )
        {
          writeHex( str, wordAt( bytes, at + 1 ) );
        }
        else
        {
          writeHex( str, byteAt( bytes, at + 1 ) );
        }
      }
      break;
    }

    case 'y': // grp 4
    case 'z': // grp 5
    {
      static constexpr std::array<std::string_view, 8> NAMES = { "INC ", "DEC ",     "CALL ", "CALL FAR ",
                                                                 "JMP ", "JMP FAR ", "PUSH ", "UNDEFINED_GRP5_7 " };
      std::uint8_t const type = ( byteAt( bytes, at + 1 ) >> 3U ) & 0x07U;
      str += NAMES[type];
      getModRmParam( str, bytes, at, segment, ( opCode & 0x01U ) != 0, type == 3 || type == 5 );
      break;
    }

    default:
      str += c;
      break;
    }
  }

  return instruction;
}

std::uint8_t getOpSize( std::span<std::uint8_t const> bytes )
{
  std::size_t at = 0;
  std::uint8_t size = 1;
  std::uint8_t opCode = byteAt( bytes, at );
  while ( isPrefix( opCode ) )
  {
    at++;
    opCode = byteAt( bytes, at );
    size++;
  }

  for ( char const c : OP_TEMPLATE[opCode] )
  {
    switch ( c )
    {
    // Absolute address
    case 'a':
      size += 2;
      break;

    // Immediate values
    case 'i':
      size++;
      break;
    case 'j':
      size += 2;
      break;

    // ModRM
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
      size += getModRmSize( byteAt( bytes, at + 1 ) );
      break;

    // Relative jumps
    case 'r':
      size++;
      break;
    case 's':
      size += 2;
      break;
    case 't':
      size += 4;
      break;

    case 'v': // grp1
      size += getModRmSize( byteAt( bytes, at + 1 ) );
      size++;
      if ( ( opCode & 0x3U ) == 0x01 )
      {
        size++;
      }
      break;

    case 'x': // grp3
    {
      std::uint8_t const modRm = byteAt( bytes, at + 1 );
      size += getModRmSize( modRm );
      if ( ( ( modRm >> 3U ) & 0x07U ) == 0 )
      {
        size = static_cast<std::uint8_t>( size + ( ( opCode & 0x01U ) != 0 ? 2 : 1 ) );
      }
      break;
    }

    case 'w': // grp2
    case 'y': // grp4
    case 'z': // grp5
      size += getModRmSize( byteAt( bytes, at + 1 ) );
      break;

    default:
      break;
    }
  }

  return size;
}

std::uint16_t getFullOpCode( std::span<std::uint8_t const> bytes )
{
  std::size_t i = 0;
  while ( isPrefix( byteAt( bytes, i ) ) && i < 6 )
  {
    i++;
  }

  return wordAt( bytes, i );
}

bool isJumpToSub( std::uint16_t opCode )
{
  switch ( static_cast<std::uint8_t>( opCode ) )
  {
  case 0x9A:
  case 0xE8:
    return true;

  case 0xFE:
  case 0xFF:
  {
    std::uint8_t const mode = ( opCode >> 11U ) & 0x07U;
    return mode == 2     // CALL
           || mode == 3; // CALL FAR
  }

  default:
    return false;
  }
}

bool isReturnInstruction( std::uint16_t opCode )
{
  switch ( static_cast<std::uint8_t>( opCode ) )
  {
  case 0xC2: // RET
  case 0xC3: // RET
  case 0xCA: // RETF
  case 0xCB: // RETF
  case 0xCF: // IRET
    return true;

  default:
    return false;
  }
}

bool isUnconditionalJump( std::uint16_t opCode )
{
  switch ( static_cast<std::uint8_t>( opCode ) )
  {
  case 0x9A: // CALL
  case 0xC2: // RET
  case 0xC3: // RET
  case 0xCA: // RETF
  case 0xCB: // RETF
  case 0xCF: // IRET
  case 0xE8: // CALL relative
  case 0xE9: // JMP
  case 0xEA: // JMP
  case 0xEB: // JMP
    return true;

  case 0xFE:
  case 0xFF:
  {
    std::uint8_t const mode = ( opCode >> 11U ) & 0x07U;
    // CALL, CALL FAR, JMP, JMP FAR
    return mode >= 2 && mode <= 5;
  }

  default:
    return false;
  }
}

bool isConditionalJump( std::uint16_t opCode )
{
  auto const op = static_cast<std::uint8_t>( opCode );
  // Jcc, LOOPNZ, LOOPZ, LOOP, JCXZ
  return ( op >= 0x70 && op <= 0x7F ) || ( op >= 0xE0 && op <= 0xE3 );
}

bool isUndefinedOpCode( std::uint16_t opCode )
{
  switch ( static_cast<std::uint8_t>( opCode ) )
  {
  case 0x0F:
  case 0x63:
  case 0x64:
  case 0x65:
  case 0x66:
  case 0x67:
  case 0xD8:
  case 0xD9:
  case 0xDA:
  case 0xDB:
  case 0xDC:
  case 0xDD:
  case 0xDE:
  case 0xDF:
  case 0xF1:
    return true;

  case 0xC0:
  case 0xC1:
  case 0xD0:
  case 0xD1:
  case 0xD2:
  case 0xD3:
    return ( ( opCode >> 11U ) & 0x07U ) == 0x06;

  case 0xF6:
  case 0xF7:
    return ( ( opCode >> 11U ) & 0x07U ) == 0x01;

  case 0xFE:
  case 0xFF:
    return ( ( opCode >> 11U ) & 0x07U ) == 0x07;

  default:
    return false;
  }
}

bool isPushPopInstruction( std::uint16_t opCode )
{
  switch ( static_cast<std::uint8_t>( opCode ) )
  {
  // Push/pop segments
  case 0x06:
  case 0x07:
  case 0x0E:
  case 0x16:
  case 0x17:
  case 0x1E:
  case 0x1F:

  // Push
  case 0x50:
  case 0x51:
  case 0x52:
  case 0x53:
  case 0x54:
  case 0x55:
  case 0x56:
  case 0x57:

  // Pop
  case 0x58:
  case 0x59:
  case 0x5A:
  case 0x5B:
  case 0x5C:
  case 0x5D:
  case 0x5E:
  case 0x5F:

  // Push/pop all
  case 0x60:
  case 0x61:

  // Push immediate
  case 0x68:
  case 0x6A:

  // Push/pop flags
  case 0x9C:
  case 0x9D:
    return true;

  default:
    return false;
  }
}

} // namespace ws::machine
