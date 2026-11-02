#pragma once

#include "ws/machine/Machine.hpp"

#include <SDL3/SDL_audio.h>

#include <atomic>
#include <memory>
#include <span>

namespace ws::app
{

/// The default playback device, fed the WonderSwan's sound at their own rate.
/// SDL's audio stream resamples them to the device's; dynamic rate control
/// nudges that resampling by up to half a percent, so that the queue stays
/// near its target however the emulation's clock and the device's drift apart.
class AudioOutput
{
public:
  /// The default device, or nothing when there is none to be had.
  static std::unique_ptr<AudioOutput> open();

  ~AudioOutput();

  AudioOutput( AudioOutput const& ) = delete;
  AudioOutput& operator=( AudioOutput const& ) = delete;
  AudioOutput( AudioOutput&& ) = delete;
  AudioOutput& operator=( AudioOutput&& ) = delete;

  /// Queues `frames`, which come at `rate` frames per second. Called from one
  /// thread at a time; the other members are safe from any.
  void push( std::span<machine::AudioFrame const> frames, double rate );

  /// Seconds of sound queued and not yet played.
  [[nodiscard]] double queuedSeconds() const;

  /// Drops what is queued: after a pause, so that sound resumes at once.
  void clear();

private:
  explicit AudioOutput( SDL_AudioStream* stream );

  SDL_AudioStream* mStream;
  /// Written by the emulation thread, read by the interface.
  std::atomic<int> mRate{};
};

} // namespace ws::app
