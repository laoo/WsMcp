#pragma once

#include "ws/Emulator.hpp"
#include "ws/control/Dispatcher.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ws::control
{

/// What the machine looked like at a checkpoint of a batch script: the frame,
/// and CRC-32s of the picture, the work RAM and the sound since the checkpoint
/// before, as eight hex digits (docs/spec/batch.md).
struct Checkpoint
{
  std::string name;
  Json sums;
};

/// How a batch script went.
struct BatchResult
{
  enum class Outcome : std::uint8_t
  {
    /// Every step ran.
    RAN,
    /// The script needs a cartridge, a boot ROM or a file that is not there.
    MISSING,
    /// A step failed, or the script is malformed.
    FAILED
  };

  Outcome outcome{};
  /// Why it did not run, when it did not.
  std::string message;
  std::vector<Checkpoint> checkpoints;
};

/// Runs a batch script, docs/spec/batch.md, on an emulator of its own made with
/// `settings`, unthrottled. A script's steps are requests of the control
/// protocol, and checkpoints at which the machine's state is summed.
[[nodiscard]] BatchResult runBatch( Settings settings, Json const& script );

/// The checkpoints of `result` that differ from the sums `golden` holds, as
/// lines a person reads; empty when every one matches. A checkpoint `golden`
/// has no sums for is reported as such.
[[nodiscard]] std::vector<std::string> compareWithGolden( BatchResult const& result, Json const& golden );

/// `result`'s checkpoints as a script's `golden` object.
[[nodiscard]] Json goldenOf( BatchResult const& result );

} // namespace ws::control
