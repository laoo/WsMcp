// Plays the machine's sound on the audio thread. The frames come at the
// machine's rate and are resampled to the device's here, linearly; the
// resampling runs up to half a percent fast or slow, so that the queue stays
// near its target however the page's clock and the device's drift apart, as
// the desktop application's audio stream does.

// How much sound the queue aims to hold: enough to ride out a late frame of
// the page, little enough not to be heard as lag.
const TARGET_SECONDS = 0.08;
const MAX_ADJUSTMENT = 0.005;
// The most the queue holds; past it the oldest sound is dropped.
const CAPACITY_SECONDS = 0.5;

class SoundProcessor extends AudioWorkletProcessor
{
  constructor( options )
  {
    super();
    this.sourceRate = options.processorOptions.sourceRate;
    this.capacity = Math.ceil( this.sourceRate * CAPACITY_SECONDS );
    this.left = new Float32Array( this.capacity );
    this.right = new Float32Array( this.capacity );
    // The oldest frame queued, how many are, and how far between it and the
    // next the output stands.
    this.read = 0;
    this.queued = 0;
    this.fraction = 0;
    this.blocks = 0;
    this.port.onmessage = ( event ) =>
    {
      if ( event.data === "clear" )
      {
        this.queued = 0;
        this.fraction = 0;
        return;
      }
      this.push( event.data );
    };
  }

  // Interleaved left and right samples, -1 to 1.
  push( samples )
  {
    const frames = samples.length / 2;
    for ( let i = 0; i < frames; ++i )
    {
      if ( this.queued === this.capacity )
      {
        this.read = ( this.read + 1 ) % this.capacity;
        --this.queued;
      }
      const at = ( this.read + this.queued ) % this.capacity;
      this.left[ at ] = samples[ i * 2 ];
      this.right[ at ] = samples[ ( i * 2 ) + 1 ];
      ++this.queued;
    }
  }

  process( inputs, outputs )
  {
    const [ left, right ] = outputs[ 0 ];
    const target = TARGET_SECONDS * this.sourceRate;
    const error = Math.max( -1, Math.min( 1, ( this.queued - target ) / target ) );
    const step = ( this.sourceRate / sampleRate ) * ( 1 + ( error * MAX_ADJUSTMENT ) );
    for ( let i = 0; i < left.length; ++i )
    {
      // Two frames are needed to blend between; without them, silence, until
      // the page catches up.
      if ( this.queued < 2 )
      {
        left[ i ] = 0;
        right[ i ] = 0;
        continue;
      }
      const next = ( this.read + 1 ) % this.capacity;
      left[ i ] = this.left[ this.read ] + ( ( this.left[ next ] - this.left[ this.read ] ) * this.fraction );
      right[ i ] = this.right[ this.read ] + ( ( this.right[ next ] - this.right[ this.read ] ) * this.fraction );
      this.fraction += step;
      while ( this.fraction >= 1 && this.queued >= 2 )
      {
        this.fraction -= 1;
        this.read = ( this.read + 1 ) % this.capacity;
        --this.queued;
      }
    }
    // About ten times a second, how much is queued, for the page to show.
    if ( ++this.blocks % 32 === 0 )
    {
      this.port.postMessage( this.queued / this.sourceRate );
    }
    return true;
  }
}

registerProcessor( "wsmcp-sound", SoundProcessor );
