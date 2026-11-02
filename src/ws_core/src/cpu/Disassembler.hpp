#pragma once

// The V30MZ's disassembler, and what a debugger asks of an opcode. Ported from
// Mesen2's Core/WS/Debugger/WsDisUtils.h and WsDisUtils.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2, without what needs Mesen2's
// debugger: labels, the code/data log and effective addresses.

#include <cstdint>
#include <span>
#include <string>

namespace ws::machine
{

struct Instruction
{
  /// Its bytes, prefixes included.
  std::uint8_t size{};
  /// In Mesen2's syntax: `MOV AX, DS:[BX+SI+$04]`, `JMP $F000:$0100`.
  std::string text;
};

/// The instruction `bytes` start with: at most 8 of them are looked at, and a
/// byte past their end reads as 0. `ip` is its offset in CS, the first prefix's
/// if it has one; a relative jump is shown as the offset it lands at.
[[nodiscard]] Instruction disassemble( std::span<std::uint8_t const> bytes, std::uint16_t ip );

/// The instruction's size, prefixes included.
[[nodiscard]] std::uint8_t getOpSize( std::span<std::uint8_t const> bytes );

/// The opcode past the prefixes, and the byte after it (the ModR/M byte, where
/// there is one) as its high byte: what the predicates below take.
[[nodiscard]] std::uint16_t getFullOpCode( std::span<std::uint8_t const> bytes );

[[nodiscard]] bool isPrefix( std::uint8_t opCode );
[[nodiscard]] bool isJumpToSub( std::uint16_t opCode );
[[nodiscard]] bool isReturnInstruction( std::uint16_t opCode );
[[nodiscard]] bool isUnconditionalJump( std::uint16_t opCode );
[[nodiscard]] bool isConditionalJump( std::uint16_t opCode );
[[nodiscard]] bool isUndefinedOpCode( std::uint16_t opCode );
[[nodiscard]] bool isPushPopInstruction( std::uint16_t opCode );

} // namespace ws::machine
