// The keyboard and the gamepads as the WonderSwan's buttons, as the desktop
// application has them (docs/decisions/0013-the-desktop-application-is-petmcps.md):
// a binding for each way the console is held, and a gamepad's directions
// turned with it. Keys are bound by their place on the keyboard
// (KeyboardEvent.code), so that WASD is where it is whatever the layout.

// The buttons in the order the page lists them, with their bits in the mask
// (ws/machine/Types.hpp's Button).
export const BUTTONS = [
  { name: "X1", bit: 0 },
  { name: "X2", bit: 3 },
  { name: "X3", bit: 1 },
  { name: "X4", bit: 2 },
  { name: "Y1", bit: 4 },
  { name: "Y2", bit: 7 },
  { name: "Y3", bit: 5 },
  { name: "Y4", bit: 6 },
  { name: "A", bit: 11 },
  { name: "B", bit: 10 },
  { name: "Start", bit: 9 },
  { name: "Sound", bit: 8 },
];

const BIT = Object.fromEntries( BUTTONS.map( ( button ) => [ button.name, 1 << button.bit ] ) );

// Mesen2's keys: flat, the X pad on the arrows and the Y pad on WASD; upright,
// the other way about, each turned with the console.
const DEFAULTS = {
  flat: [ "ArrowUp", "ArrowRight", "ArrowDown", "ArrowLeft", "KeyW", "KeyD", "KeyS", "KeyA", "KeyX", "KeyZ", "Enter", "KeyQ" ],
  upright: [ "KeyA", "KeyW", "KeyD", "KeyS", "ArrowLeft", "ArrowUp", "ArrowRight", "ArrowDown", "KeyZ", "KeyX", "Enter", "KeyQ" ],
};

const STORAGE_KEY = "wsmcp.keys";

// How far a stick is pushed before it counts as a direction.
const STICK_THRESHOLD = 0.5;

// The standard mapping's buttons (https://w3c.github.io/gamepad/#remapping).
const PAD = { south: 0, east: 1, west: 2, north: 3, left: 4, right: 5, back: 8, start: 9, up: 12, down: 13, leftPad: 14, rightPad: 15 };

// `directions`, up, right, down and left, as the buttons `names` name in turn.
function padOf( directions, names )
{
  return directions.reduce( ( mask, on, i ) => ( on ? mask | BIT[ names[ i ] ] : mask ), 0 );
}

function stickOf( pad, x, y )
{
  const dx = pad.axes[ x ] ?? 0;
  const dy = pad.axes[ y ] ?? 0;
  return [ dy < -STICK_THRESHOLD, dx > STICK_THRESHOLD, dy > STICK_THRESHOLD, dx < -STICK_THRESHOLD ];
}

// What a gamepad holds. Flat, its pad and left stick are the X pad, its right
// stick the Y pad, east and south A and B. Upright, the console's left goes
// to the bottom: its pad and left stick are the Y pad, its face buttons and
// right stick the X pad, each turned a quarter, and its shoulders A and B.
function buttonsOf( pad, way )
{
  const pressed = ( index ) => pad.buttons[ index ]?.pressed ?? false;
  const left = stickOf( pad, 0, 1 );
  const directions = [ PAD.up, PAD.rightPad, PAD.down, PAD.leftPad ].map( ( index, i ) => left[ i ] || pressed( index ) );
  const right = stickOf( pad, 2, 3 );
  let mask = ( pressed( PAD.start ) ? BIT.Start : 0 ) | ( pressed( PAD.back ) ? BIT.Sound : 0 );
  if ( way === "flat" )
  {
    mask |= padOf( directions, [ "X1", "X2", "X3", "X4" ] );
    mask |= padOf( right, [ "Y1", "Y2", "Y3", "Y4" ] );
    mask |= ( pressed( PAD.east ) ? BIT.A : 0 ) | ( pressed( PAD.south ) ? BIT.B : 0 );
    return mask;
  }
  const xTurned = [ "X2", "X3", "X4", "X1" ];
  const face = [ pressed( PAD.north ), pressed( PAD.east ), pressed( PAD.south ), pressed( PAD.west ) ];
  mask |= padOf( directions, [ "Y2", "Y3", "Y4", "Y1" ] );
  mask |= padOf( face, xTurned ) | padOf( right, xTurned );
  mask |= ( pressed( PAD.right ) ? BIT.A : 0 ) | ( pressed( PAD.left ) ? BIT.B : 0 );
  return mask;
}

export class Input
{
  constructor()
  {
    this.way = "flat";
    this.bindings = { flat: [ ...DEFAULTS.flat ], upright: [ ...DEFAULTS.upright ] };
    // What each key down holds, by its code.
    this.held = new Map();
    // The binding the next key pressed becomes, and who is told.
    this.capturing = null;
    try
    {
      const kept = JSON.parse( localStorage.getItem( STORAGE_KEY ) ?? "null" );
      for ( const way of [ "flat", "upright" ] )
      {
        if ( Array.isArray( kept?.[ way ] ) && kept[ way ].length === BUTTONS.length )
        {
          this.bindings[ way ] = kept[ way ].map( String );
        }
      }
    }
    catch
    {
      // No storage, or nothing readable in it: the defaults.
    }
  }

  save()
  {
    try
    {
      localStorage.setItem( STORAGE_KEY, JSON.stringify( this.bindings ) );
    }
    catch
    {
      // Kept for this visit only.
    }
  }

  reset()
  {
    this.bindings = { flat: [ ...DEFAULTS.flat ], upright: [ ...DEFAULTS.upright ] };
    this.save();
  }

  // The next key pressed becomes binding `index` of `way`, and `done` is told;
  // Escape keeps it as it is.
  capture( way, index, done )
  {
    this.capturing = { way, index, done };
  }

  // Takes a key going down; whether it was the console's.
  keyDown( event )
  {
    if ( this.capturing )
    {
      const { way, index, done } = this.capturing;
      this.capturing = null;
      if ( event.code !== "Escape" )
      {
        this.bindings[ way ][ index ] = event.code;
        this.save();
      }
      done();
      return true;
    }
    if ( event.ctrlKey || event.metaKey || event.altKey )
    {
      return false;
    }
    const index = this.bindings[ this.way ].indexOf( event.code );
    if ( index < 0 )
    {
      return false;
    }
    this.held.set( event.code, BIT[ BUTTONS[ index ].name ] );
    return true;
  }

  keyUp( event )
  {
    return this.held.delete( event.code );
  }

  // Lets go of every key: when the page loses the keyboard.
  releaseAll()
  {
    this.held.clear();
  }

  // The buttons held, as the mask's bits.
  buttons()
  {
    let mask = 0;
    for ( const bit of this.held.values() )
    {
      mask |= bit;
    }
    for ( const pad of this.gamepads() )
    {
      mask |= buttonsOf( pad, this.way );
    }
    return mask;
  }

  gamepads()
  {
    return navigator.getGamepads ? [ ...navigator.getGamepads() ].filter( ( pad ) => pad?.connected ) : [];
  }
}
