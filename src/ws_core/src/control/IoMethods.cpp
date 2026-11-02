#include "Methods.hpp"

#include <vector>

namespace ws::control
{

namespace
{

/// The V30MZ's I/O space; the WonderSwan decodes $00-$FF of it.
constexpr std::uint64_t PORT_SPACE = 0x10000;
constexpr std::uint64_t MAX_PORTS = 0x100;

std::expected<std::uint16_t, Error> portOf( Json const& params )
{
  auto const port = requireUnsigned( params, "port" );
  if ( !port || *port >= PORT_SPACE )
  {
    return std::unexpected( badRequest( "port is 0 to $FFFF" ) );
  }
  return static_cast<std::uint16_t>( *port );
}

char const* nameOf( machine::VideoMode mode )
{
  switch ( mode )
  {
  case machine::VideoMode::MONOCHROME:
    return "monochrome";
  case machine::VideoMode::COLOR_2BPP:
    return "color_2bpp";
  case machine::VideoMode::COLOR_4BPP:
    return "color_4bpp";
  case machine::VideoMode::COLOR_4BPP_PACKED:
    return "color_4bpp_packed";
  }
  return "?";
}

char const* nameOf( machine::EepromSize size )
{
  switch ( size )
  {
  case machine::EepromSize::SIZE_0:
    return "none";
  case machine::EepromSize::SIZE_128:
    return "128 B";
  case machine::EepromSize::SIZE_1KB:
    return "1 KB";
  case machine::EepromSize::SIZE_2KB:
    return "2 KB";
  }
  return "?";
}

/// A register and its latch, the copy the PPU draws the line with.
Json latched( auto value, auto latch )
{
  return Json{ { "value", value }, { "latch", latch } };
}

Json layerJson( machine::BgLayer const& layer )
{
  return Json{ { "enabled", latched( layer.enabled, layer.enabledLatch ) },
               { "map_address", latched( layer.mapAddress, layer.mapAddressLatch ) },
               { "scroll_x", latched( layer.scrollX, layer.scrollXLatch ) },
               { "scroll_y", latched( layer.scrollY, layer.scrollYLatch ) } };
}

Json windowJson( machine::Window const& window )
{
  return Json{ { "enabled", latched( window.enabled, window.enabledLatch ) },
               { "left", latched( window.left, window.leftLatch ) },
               { "right", latched( window.right, window.rightLatch ) },
               { "top", latched( window.top, window.topLatch ) },
               { "bottom", latched( window.bottom, window.bottomLatch ) } };
}

Json ppuJson( machine::PpuState const& ppu )
{
  return Json{ { "frame_count", ppu.frameCount },
               { "scanline", ppu.scanline },
               { "cycle", ppu.cycle },
               { "control", ppu.control },
               { "mode", nameOf( ppu.mode ) },
               { "next_mode", nameOf( ppu.nextMode ) },
               { "lcd_enabled", ppu.lcdEnabled },
               { "lcd_control", ppu.lcdControl },
               { "high_contrast", ppu.highContrast },
               { "sleep_enabled", ppu.sleepEnabled },
               { "screen_address", ppu.screenAddress },
               { "bg_color", ppu.bgColor },
               { "irq_scanline", ppu.irqScanline },
               { "last_scanline", ppu.lastScanline },
               { "back_porch_scanline", ppu.backPorchScanline },
               { "screen1", layerJson( ppu.bgLayers[0] ) },
               { "screen2", layerJson( ppu.bgLayers[1] ) },
               { "screen2_window", windowJson( ppu.bgWindow ) },
               { "draw_outside_screen2_window", latched( ppu.drawOutsideBgWindow, ppu.drawOutsideBgWindowLatch ) },
               { "sprite_window", windowJson( ppu.spriteWindow ) },
               { "sprites_enabled", latched( ppu.spritesEnabled, ppu.spritesEnabledLatch ) },
               { "sprite_table_address", ppu.spriteTableAddress },
               { "first_sprite", ppu.firstSpriteIndex },
               { "sprite_count", latched( ppu.spriteCount, ppu.spriteCountLatch ) },
               { "bw_shades", ppu.bwShades },
               { "bw_palettes", ppu.bwPalettes },
               { "icons",
                 { { "sleep", ppu.icons.sleep },
                   { "vertical", ppu.icons.vertical },
                   { "horizontal", ppu.icons.horizontal },
                   { "aux1", ppu.icons.aux1 },
                   { "aux2", ppu.icons.aux2 },
                   { "aux3", ppu.icons.aux3 },
                   { "value", ppu.icons.value } } },
               { "lcd_tft_config", ppu.lcdTftConfig } };
}

Json channelJson( machine::BaseApuState const& channel )
{
  return Json{ { "enabled", channel.enabled },
               { "frequency", channel.frequency },
               { "timer", channel.timer },
               { "left_volume", channel.leftVolume },
               { "right_volume", channel.rightVolume },
               { "sample_position", channel.samplePosition },
               { "left_output", channel.leftOutput },
               { "right_output", channel.rightOutput } };
}

Json apuJson( machine::ApuState const& apu )
{
  Json ch2 = channelJson( apu.ch2 );
  ch2.update( Json{ { "pcm_enabled", apu.ch2.pcmEnabled },
                    { "max_pcm_volume_left", apu.ch2.maxPcmVolumeLeft },
                    { "half_pcm_volume_left", apu.ch2.halfPcmVolumeLeft },
                    { "max_pcm_volume_right", apu.ch2.maxPcmVolumeRight },
                    { "half_pcm_volume_right", apu.ch2.halfPcmVolumeRight } } );
  Json ch3 = channelJson( apu.ch3 );
  ch3.update( Json{ { "sweep_enabled", apu.ch3.sweepEnabled },
                    { "sweep_value", apu.ch3.sweepValue },
                    { "sweep_period", apu.ch3.sweepPeriod },
                    { "sweep_timer", apu.ch3.sweepTimer },
                    { "sweep_scaler", apu.ch3.sweepScaler },
                    { "sweep_cpu_clock", apu.ch3.useSweepCpuClock } } );
  Json ch4 = channelJson( apu.ch4 );
  ch4.update( Json{ { "noise_enabled", apu.ch4.noiseEnabled },
                    { "lfsr_enabled", apu.ch4.lfsrEnabled },
                    { "tap_mode", apu.ch4.tapMode },
                    { "tap_shift", apu.ch4.tapShift },
                    { "lfsr", apu.ch4.lfsr },
                    { "hold_lfsr", apu.ch4.holdLfsr } } );
  machine::ApuHyperVoiceState const& voice = apu.voice;
  return Json{ { "ch1", channelJson( apu.ch1 ) },
               { "ch2", std::move( ch2 ) },
               { "ch3", std::move( ch3 ) },
               { "ch4", std::move( ch4 ) },
               { "hyper_voice",
                 { { "enabled", voice.enabled },
                   { "control_low", voice.controlLow },
                   { "control_high", voice.controlHigh },
                   { "left_output", voice.leftOutput },
                   { "right_output", voice.rightOutput },
                   { "left_sample", voice.leftSample },
                   { "right_sample", voice.rightSample },
                   { "divisor", voice.divisor },
                   { "timer", voice.timer },
                   { "input", voice.input },
                   { "shift", voice.shift } } },
               { "wave_table_address", apu.waveTableAddress },
               { "speaker_enabled", apu.speakerEnabled },
               { "speaker_volume", apu.speakerVolume },
               { "headphone_enabled", apu.headphoneEnabled },
               { "master_volume", apu.masterVolume },
               { "internal_master_volume", apu.internalMasterVolume },
               { "hold_channels", apu.holdChannels },
               { "sound_test", apu.soundTest } };
}

Json eepromJson( machine::EepromState const& eeprom )
{
  return Json{ { "size", nameOf( eeprom.size ) },
               { "command", eeprom.command },
               { "control", eeprom.control },
               { "read_buffer", eeprom.readBuffer },
               { "write_buffer", eeprom.writeBuffer },
               { "write_disabled", eeprom.writeDisabled },
               { "read_done", eeprom.readDone },
               { "idle", eeprom.idle },
               { "internal_write_protected", eeprom.internalEepromWriteProtected } };
}

/// The 2003's own ports and its RTC: the S-3511A's registers and where the
/// command running stands.
Json bandai2003Json( machine::CartState const& cart )
{
  machine::RtcState const& rtc = cart.rtc;
  static constexpr std::array<char const*, 5> PHASES{ "idle", "command", "payload", "wait_write", "wait_read" };
  return Json{ { "gpo_enable", cart.gpoEnable },
               { "gpo_data", cart.gpoData },
               { "flash_selected", cart.flashSelected },
               { "rtc",
                 { { "date_time", rtc.dateTime },
                   { "config", rtc.config },
                   { "alarm", rtc.alarm },
                   { "control", ( rtc.ready ? 0x80U : 0U ) | rtc.command },
                   { "data", rtc.data },
                   { "phase", PHASES.at( static_cast<std::size_t>( rtc.phase ) ) },
                   { "payload_index", rtc.index } } } };
}

Json stateJson( machine::State const& state, bool bandai2003 )
{
  machine::MemoryManagerState const& memory = state.memoryManager;
  machine::DmaControllerState const& dma = state.dmaController;
  machine::TimerState const& timer = state.timer;
  machine::SerialState const& serial = state.serial;
  Json json{ { "model", modelName( state.model ) },
             { "ppu", ppuJson( state.ppu ) },
             { "apu", apuJson( state.apu ) },
             { "timer",
               { { "control", timer.control },
                 { "h_timer", timer.hTimer },
                 { "h_reload", timer.hReloadValue },
                 { "h_enabled", timer.hBlankEnabled },
                 { "h_auto_reload", timer.hBlankAutoReload },
                 { "v_timer", timer.vTimer },
                 { "v_reload", timer.vReloadValue },
                 { "v_enabled", timer.vBlankEnabled },
                 { "v_auto_reload", timer.vBlankAutoReload } } },
             { "dma",
               { { "gdma_source", dma.gdmaSrc },
                 { "gdma_destination", dma.gdmaDest },
                 { "gdma_length", dma.gdmaLength },
                 { "gdma_control", dma.gdmaControl },
                 { "sdma_source", dma.sdmaSrc },
                 { "sdma_length", dma.sdmaLength },
                 { "sdma_source_reload", dma.sdmaSrcReloadValue },
                 { "sdma_length_reload", dma.sdmaLengthReloadValue },
                 { "sdma_control", dma.sdmaControl },
                 { "sdma_enabled", dma.sdmaEnabled },
                 { "sdma_decrement", dma.sdmaDecrement },
                 { "sdma_hyper_voice", dma.sdmaHyperVoice },
                 { "sdma_repeat", dma.sdmaRepeat },
                 { "sdma_hold", dma.sdmaHold },
                 { "sdma_frequency", dma.sdmaFrequency },
                 { "sdma_timer", dma.sdmaTimer } } },
             { "memory_manager",
               { { "active_irqs", memory.activeIrqs },
                 { "enabled_irqs", memory.enabledIrqs },
                 { "irq_vector_offset", memory.irqVectorOffset },
                 { "system_control2", memory.systemControl2 },
                 { "system_test", memory.systemTest },
                 { "color_enabled", memory.colorEnabled },
                 { "enable_4bpp", memory.enable4bpp },
                 { "enable_4bpp_packed", memory.enable4bppPacked },
                 { "boot_rom_disabled", memory.bootRomDisabled },
                 { "cart_word_bus", memory.cartWordBus },
                 { "slow_rom", memory.slowRom },
                 { "slow_sram", memory.slowSram },
                 { "slow_port", memory.slowPort },
                 { "low_battery_nmi", memory.enableLowBatteryNmi },
                 { "power_off_requested", memory.powerOffRequested } } },
             { "keypad", { { "input_select", state.controlManager.inputSelect } } },
             { "serial",
               { { "enabled", serial.enabled },
                 { "baud", serial.highSpeed ? 38400 : 9600 },
                 { "has_send_data", serial.hasSendData },
                 { "send_buffer", serial.sendBuffer },
                 { "has_receive_data", serial.hasReceiveData },
                 { "receive_buffer", serial.receiveBuffer },
                 { "receive_overflow", serial.receiveOverflow } } },
             { "internal_eeprom", eepromJson( state.internalEeprom ) },
             { "cart", { { "banks", state.cart.selectedBanks }, { "eeprom", eepromJson( state.cartEeprom ) } } } };
  if ( bandai2003 )
  {
    json["cart"]["bandai2003"] = bandai2003Json( state.cart );
  }
  return json;
}

} // namespace

void addIoMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "io.read",
      info( "Reads I/O ports as the V30MZ's IN would, but without a read's side effects: the serial port's data "
            "and status, $B1 and $B3, and the keypad's, $B5, read 0. The WonderSwan's ports are $00-$FF; the "
            "16-bit I/O space above them mirrors them or answers nothing.",
            { { .name = "port", .type = "integer", .description = "The first port, 0 to $FFFF." },
              { .name = "count",
                .type = "integer",
                .description = "How many consecutive ports, 1 to 256; 1 when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const port = portOf( params );
        if ( !port )
        {
          return std::unexpected( port.error() );
        }
        std::uint64_t count = 1;
        if ( params.contains( "count" ) )
        {
          auto const given = requireUnsigned( params, "count" );
          if ( !given || *given == 0 || *given > MAX_PORTS || *port + *given > PORT_SPACE )
          {
            return std::unexpected( badRequest( "count is 1 to 256, within ports 0 to $FFFF" ) );
          }
          count = *given;
        }
        std::vector<std::uint8_t> values;
        values.reserve( static_cast<std::size_t>( count ) );
        for ( std::uint64_t i = 0; i < count; ++i )
        {
          values.push_back( ( *machine )->peekPort( static_cast<std::uint16_t>( *port + i ) ) );
        }
        return Json{ { "port", *port }, { "values", values }, { "data_hex", hexOf( values ) } };
      } );

  dispatcher.add( "io.write",
                  info( "Writes an I/O port as the V30MZ's OUT would, with every effect the write has, but beside the "
                        "bus: no cycle passes.",
                        { { .name = "port", .type = "integer", .description = "The port, 0 to $FFFF." },
                          { .name = "value", .type = "integer", .description = "The byte, 0 to $FF." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    auto const port = portOf( params );
                    if ( !port )
                    {
                      return std::unexpected( port.error() );
                    }
                    auto const value = requireUnsigned( params, "value" );
                    if ( !value || *value > 0xff )
                    {
                      return std::unexpected( badRequest( "value is a byte, 0 to $FF" ) );
                    }
                    ( *machine )->pokePort( *port, static_cast<std::uint8_t>( *value ) );
                    return Json::object();
                  } );

  dispatcher.add(
      "io.get_state",
      info( "Every device's registers as the debugger keeps them: the PPU (mode, screens 1 and 2 with their scroll "
            "and map, the windows, sprites, LCD icons and shades; a latched register with the copy the line is "
            "drawn with), the APU's four channels and Hyper Voice, the timers, the DMAs, the interrupt controller "
            "(active and enabled IRQs, vector), the keypad, the serial port, the console's and the cartridge's "
            "EEPROMs, and the cartridge's bank registers ($C0-$C3)." ),
      [&emulator]( Json const& /*params*/ ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        return stateJson( ( *machine )->state(), ( *machine )->hasRtc() );
      } );
}

} // namespace ws::control
