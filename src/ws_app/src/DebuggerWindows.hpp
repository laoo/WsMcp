#pragma once

#include "ws/control/Dispatcher.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace ws::app
{

/// How a window asks the emulator: a method of the control protocol and its
/// params, answered with the whole response.
using Requester = std::function<control::Json( std::string const& method, control::Json params )>;

/// An address as the protocol takes it, typed: "SSSS:OOOO", or a linear one
/// in hex.
using AddressText = std::array<char, 16>;

/// The V30MZ: its registers, the code at CS:IP, breakpoints, and stepping.
/// Everything it shows and does goes through the protocol, as an agent's
/// requests do.
class CpuWindow
{
public:
  explicit CpuWindow( Requester request );
  void draw( bool& open );

private:
  void drawBreakpoints();

  Requester mRequest;
  AddressText mNewBreakpoint{};
  std::string mLastError;
};

/// A region of memory as hex and characters, as memory.read reads it: without
/// a read's side effects.
class MemoryWindow
{
public:
  explicit MemoryWindow( Requester request );
  void draw( bool& open );

private:
  Requester mRequest;
  std::string mRegion{ "cpu" };
  AddressText mGoTo{};
  bool mScrollToGoTo{};
  std::string mLastError;
};

/// The history of execution: the instructions the V30MZ began over the last
/// seconds kept, as they ran, with the registers before each.
class HistoryWindow
{
public:
  /// `keep` is told the seconds the history is set to keep, to keep them for
  /// the next start.
  HistoryWindow( Requester request, std::function<void( double seconds )> keep );
  void draw( bool& open );

private:
  Requester mRequest;
  std::function<void( double )> mKeep;
  /// The seconds being typed, and whether they have been read from the
  /// emulator yet.
  double mSeconds{ 0.5 };
  bool mSecondsKnown{};
  /// The search: an address, a text of the disassembly, either or both, and
  /// what it found, as debug.trace.find answers.
  AddressText mFindPc{};
  bool mFindByPc{};
  std::array<char, 32> mFindText{};
  control::Json mFound;
  std::string mLastError;
  /// An index of the history to bring into view.
  std::optional<std::int64_t> mShowIndex;
  /// Whether the view keeps to the newest instruction.
  bool mFollow{ true };
};

/// Every device's registers, as io.get_state answers them, as a tree.
class IoWindow
{
public:
  explicit IoWindow( Requester request );
  void draw( bool& open );

private:
  Requester mRequest;
};

} // namespace ws::app
