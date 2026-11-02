// The page: the core built for WebAssembly (src/ws_web), a canvas, Web Audio,
// the keyboard and gamepads, and the files it is given, kept in IndexedDB
// (docs/decisions/0014-the-page-keeps-its-files-in-indexeddb.md).

import createWsMcp from "./wsmcp.js";
import { BUTTONS, Input } from "./input.js";
import { Sound } from "./sound.js";

// The WonderSwan's frame rate, some 75.47 Hz, and the most frames run to
// catch up after the page stalled: the rest is dropped.
const FRAMES_PER_SECOND = 3_072_000 / ( 256 * 159 );
const MAX_FRAMES_OWED = 4;
const AUDIO_RATE = 24_000;
// How often the saves are written to IndexedDB while a game runs.
const SAVE_EVERY_MS = 30_000;

// The directories kept in IndexedDB. The core looks for boot ROMs in /bios,
// keeps saves in /saves and states in /states (src/ws_web/src/Bindings.cpp).
const CARTRIDGES = "/cartridges";
const KEPT = [ CARTRIDGES, "/bios", "/saves", "/states" ];
const BOOT_ROMS = {
  "bootrom.ws": "WonderSwan",
  "bootrom.wsc": "WonderSwan Color",
  "bootrom_sc.wsc": "SwanCrystal",
  "[BIOS] WonderSwan Boot ROM (Japan) (En).ws": "WonderSwan",
  "[BIOS] WonderSwan Color Boot ROM (Japan) (En).wsc": "WonderSwan Color",
  "[BIOS] SwanCrystal Boot ROM (Japan) (En).wsc": "SwanCrystal",
};
const MODELS = { ws: "WonderSwan", wsc: "WonderSwan Color", sc: "SwanCrystal" };

const SETTINGS_KEY = "wsmcp.settings";

const $ = ( id ) => document.getElementById( id );

const settings = { model: "", upright: true, whole: true, volume: 0.8, last: "" };
try
{
  Object.assign( settings, JSON.parse( localStorage.getItem( SETTINGS_KEY ) ?? "{}" ) );
}
catch
{
  // No storage: the defaults, for this visit.
}

function keepSettings()
{
  try
  {
    localStorage.setItem( SETTINGS_KEY, JSON.stringify( settings ) );
  }
  catch
  {
    // Kept for this visit only.
  }
}

function showMessage( text, error = false )
{
  const message = $( "message" );
  message.textContent = text;
  message.hidden = !text;
  message.classList.toggle( "error", error );
}

// The module, with the kept directories read back from IndexedDB.
const module = await createWsMcp();
const { FS, IDBFS } = module;
for ( const directory of KEPT )
{
  FS.mkdir( directory );
  FS.mount( IDBFS, {}, directory );
}

// One synchronisation at a time: IDBFS does not take two at once.
let syncing = Promise.resolve();
function sync( fromIndexedDb )
{
  syncing = syncing.then( () => new Promise( ( resolve ) =>
  {
    FS.syncfs( fromIndexedDb, ( error ) =>
    {
      if ( error )
      {
        console.warn( "IndexedDB:", error );
      }
      resolve();
    } );
  } ) );
  return syncing;
}
await sync( true );

const ws = new module.WsMcp();
const input = new Input();
const sound = new Sound( AUDIO_RATE );
sound.setVolume( settings.volume );

function request( method, params = {} )
{
  return JSON.parse( ws.handle( JSON.stringify( { id: 1, method, params } ) ) );
}

function filesIn( directory )
{
  return FS.readdir( directory ).filter( ( name ) => name !== "." && name !== ".." ).sort();
}

// --- The screen ---

const canvas = $( "screen" );
const context = canvas.getContext( "2d" );
const source = document.createElement( "canvas" );
const sourceContext = source.getContext( "2d" );
let image = null;

// Sized to the stage, in whole multiples of the display's pixels when asked:
// the dots stay square and even.
function fit()
{
  const stage = $( "stage" ).getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  let scale = Math.min( stage.width / canvas.width, stage.height / canvas.height );
  if ( settings.whole )
  {
    scale = Math.max( 1, Math.floor( scale * ratio ) ) / ratio;
  }
  canvas.style.width = `${ canvas.width * scale }px`;
  canvas.style.height = `${ canvas.height * scale }px`;
}
new ResizeObserver( fit ).observe( $( "stage" ) );

// Draws the last picture, turned a quarter anticlockwise when the cartridge
// is held upright, as Mesen2 and the desktop application turn it.
function draw()
{
  const pixels = ws.picture();
  if ( !pixels )
  {
    return;
  }
  const width = ws.width();
  const height = ws.height();
  if ( !image || image.width !== width || image.height !== height )
  {
    source.width = width;
    source.height = height;
    image = new ImageData( width, height );
  }
  image.data.set( pixels );
  sourceContext.putImageData( image, 0, 0 );

  const upright = settings.upright && ws.vertical();
  input.way = upright ? "upright" : "flat";
  const shownWidth = upright ? height : width;
  const shownHeight = upright ? width : height;
  if ( canvas.width !== shownWidth || canvas.height !== shownHeight )
  {
    canvas.width = shownWidth;
    canvas.height = shownHeight;
    fit();
  }
  context.imageSmoothingEnabled = false;
  // Upright, a dot at (x, y) is shown at (y, width - x): the LCD's right edge
  // is the top.
  context.setTransform( ...( upright ? [ 0, -1, 1, 0, 0, width ] : [ 1, 0, 0, 1, 0, 0 ] ) );
  context.drawImage( source, 0, 0 );
}

// --- Running ---

let running = false;
let paused = false;
let owed = 0;
let last = performance.now();
let framesThisSecond = 0;
let framesPerSecond = 0;

function setPaused( value )
{
  paused = value;
  $( "pause" ).textContent = paused ? "Run" : "Pause";
  if ( paused )
  {
    sound.clear();
  }
}

// The host's clock paces the machine at its own rate, whatever the display's.
function tick( now )
{
  const elapsed = Math.min( ( now - last ) / 1000, 0.25 );
  last = now;
  if ( running && !paused )
  {
    owed = Math.min( owed + ( elapsed * FRAMES_PER_SECOND ), MAX_FRAMES_OWED );
    let ran = false;
    while ( owed >= 1 )
    {
      owed -= 1;
      ws.setButtons( input.buttons() );
      if ( ws.runFrame() === 0 )
      {
        // A breakpoint or a watchpoint set through the protocol stopped it.
        setPaused( true );
        showMessage( "Stopped where the debugger asked; emu.status says where. Run goes on." );
        break;
      }
      sound.push( ws.audio() );
      ++framesThisSecond;
      ran = true;
    }
    if ( ran )
    {
      draw();
    }
  }
  else
  {
    owed = 0;
  }
  requestAnimationFrame( tick );
}
requestAnimationFrame( tick );

setInterval( () =>
{
  framesPerSecond = framesThisSecond;
  framesThisSecond = 0;
  showStatus();
  showHeld();
}, 1000 );

// Writes the saves into /saves, and the kept directories to IndexedDB.
async function keepSaves()
{
  if ( running )
  {
    const error = ws.flushSaves();
    if ( error )
    {
      showMessage( error, true );
    }
  }
  await sync( false );
}
setInterval( keepSaves, SAVE_EVERY_MS );
document.addEventListener( "visibilitychange", () =>
{
  if ( document.visibilityState === "hidden" )
  {
    input.releaseAll();
    keepSaves();
  }
} );

// --- Files ---

// The name a boot ROM is kept under, as the core looks for it; null when the
// file is no boot ROM. A WonderSwan's is 4 KB; a Color's and a SwanCrystal's
// are 8 KB each, told apart by their names.
function bootRomName( name, size )
{
  if ( name in BOOT_ROMS )
  {
    return name;
  }
  const lower = name.toLowerCase();
  const named = lower.includes( "boot" ) || lower.includes( "[bios]" ) || lower.includes( "ipl" );
  if ( size > 8192 || ( size !== 4096 && size !== 8192 && !named ) )
  {
    return null;
  }
  if ( size === 4096 )
  {
    return "bootrom.ws";
  }
  return lower.includes( "crystal" ) ? "bootrom_sc.wsc" : "bootrom.wsc";
}

// A file name the file system takes.
function safeName( name )
{
  return name.replace( /[/\\]/g, "_" ) || "cartridge.ws";
}

async function putCartridgeIn( path )
{
  const error = ws.load( path, settings.model );
  if ( error )
  {
    showMessage( error, true );
    return;
  }
  running = true;
  settings.last = path;
  keepSettings();
  showMessage( "" );
  setPaused( false );
  sound.clear();
  $( "welcome" ).hidden = true;
  for ( const id of [ "pause", "power", "save-state", "load-state" ] )
  {
    $( id ).disabled = false;
  }
  document.title = `${ path.slice( CARTRIDGES.length + 1 ) } - WsMcp`;
  draw();
  showFiles();
  await sync( false );
}

// Keeps a file given to the page: a boot ROM in /bios, a cartridge in
// /cartridges, which is then put in.
async function take( name, bytes )
{
  const bootRom = bootRomName( name, bytes.length );
  if ( bootRom )
  {
    FS.writeFile( `/bios/${ bootRom }`, bytes );
    await sync( false );
    showFiles();
    showMessage( `The ${ BOOT_ROMS[ bootRom ] }'s boot ROM is kept. It runs at the next power-up${ running ? ": Power off and on" : "" }.` );
    return;
  }
  const path = `${ CARTRIDGES }/${ safeName( name ) }`;
  FS.writeFile( path, bytes );
  await putCartridgeIn( path );
}

async function takeFiles( files )
{
  for ( const file of files )
  {
    await take( file.name, new Uint8Array( await file.arrayBuffer() ) );
  }
}

function showFiles()
{
  const kept = $( "kept" );
  kept.replaceChildren( new Option( "Kept cartridges", "" ) );
  for ( const name of filesIn( CARTRIDGES ) )
  {
    kept.add( new Option( name, `${ CARTRIDGES }/${ name }` ) );
  }
  kept.value = "";

  const list = $( "files" );
  list.replaceChildren();
  for ( const directory of KEPT )
  {
    for ( const name of filesIn( directory ) )
    {
      const item = document.createElement( "li" );
      const label = document.createElement( "span" );
      label.textContent = `${ directory }/${ name }`;
      const forget = document.createElement( "button" );
      forget.type = "button";
      forget.textContent = "Forget";
      forget.title = "Delete it from this browser";
      forget.addEventListener( "click", async () =>
      {
        FS.unlink( `${ directory }/${ name }` );
        await sync( false );
        showFiles();
      } );
      item.append( label, forget );
      list.append( item );
    }
  }
  if ( !list.childElementCount )
  {
    list.textContent = "Nothing yet.";
  }
}

// --- Status ---

function fact( list, name, value )
{
  const term = document.createElement( "dt" );
  term.textContent = name;
  const detail = document.createElement( "dd" );
  detail.textContent = value;
  list.append( term, detail );
}

function showStatus()
{
  const list = $( "status" );
  list.replaceChildren();
  const status = request( "emu.status" ).result;
  const bootRoms = filesIn( "/bios" ).map( ( name ) => BOOT_ROMS[ name ] ).filter( Boolean );
  if ( status.running )
  {
    fact( list, "Cartridge", status.cartridge.slice( CARTRIDGES.length + 1 ) );
    fact( list, "Model", MODELS[ status.model ] ?? status.model );
    fact( list, "Boot ROM", status.boot_rom ? "ran" : "none for this model" );
    fact( list, "Frame", String( status.frame ) );
    fact( list, "Speed", paused ? "paused" : `${ framesPerSecond } frames a second` );
    if ( ws.vertical() )
    {
      fact( list, "Held", settings.upright ? "upright" : "upright, shown flat" );
    }
  }
  else
  {
    fact( list, "Cartridge", "none" );
  }
  fact( list, "Boot ROMs", bootRoms.length ? bootRoms.join( ", " ) : "none" );
  fact( list, "Sound", sound.started ? `${ Math.round( sound.queuedSeconds * 1000 ) } ms queued` : "off until a click or key" );
  const pads = input.gamepads();
  $( "gamepads" ).textContent = pads.length ? `Gamepads: ${ pads.map( ( pad ) => pad.id ).join( "; " ) }` : "No gamepad; press one of its buttons to show it.";
}

// --- Keys ---

function keyName( code )
{
  const arrows = { ArrowUp: "↑", ArrowDown: "↓", ArrowLeft: "←", ArrowRight: "→" };
  return arrows[ code ] ?? code.replace( /^Key|^Digit/, "" );
}

function showKeys()
{
  const rows = BUTTONS.map( ( button, index ) =>
  {
    const row = document.createElement( "tr" );
    row.dataset.bit = String( button.bit );
    const name = document.createElement( "td" );
    name.textContent = button.name;
    row.append( name );
    for ( const way of [ "flat", "upright" ] )
    {
      const cell = document.createElement( "td" );
      const bind = document.createElement( "button" );
      bind.type = "button";
      bind.textContent = keyName( input.bindings[ way ][ index ] );
      bind.addEventListener( "click", () =>
      {
        bind.textContent = "press a key…";
        input.capture( way, index, showKeys );
      } );
      cell.append( bind );
      row.append( cell );
    }
    return row;
  } );
  $( "keys" ).replaceChildren( ...rows );
}

// The buttons held, their names lit.
function showHeld()
{
  const held = input.buttons();
  for ( const row of $( "keys" ).rows )
  {
    row.classList.toggle( "held", ( held & ( 1 << Number( row.dataset.bit ) ) ) !== 0 );
  }
}

// --- Wiring ---

function isField( target )
{
  return target instanceof HTMLTextAreaElement || ( target instanceof HTMLInputElement && target.type !== "checkbox" && target.type !== "range" ) || target instanceof HTMLSelectElement;
}

window.addEventListener( "keydown", ( event ) =>
{
  sound.start();
  if ( !input.capturing && isField( event.target ) )
  {
    return;
  }
  if ( input.keyDown( event ) )
  {
    event.preventDefault();
    showHeld();
  }
} );
window.addEventListener( "keyup", ( event ) =>
{
  if ( input.keyUp( event ) )
  {
    event.preventDefault();
    showHeld();
  }
} );
window.addEventListener( "blur", () => input.releaseAll() );
window.addEventListener( "pointerdown", () => sound.start() );

$( "open" ).addEventListener( "change", async ( event ) =>
{
  await takeFiles( event.target.files );
  event.target.value = "";
} );
$( "bios" ).addEventListener( "change", async ( event ) =>
{
  for ( const file of event.target.files )
  {
    const bytes = new Uint8Array( await file.arrayBuffer() );
    if ( !bootRomName( file.name, bytes.length ) )
    {
      showMessage( `${ file.name } is no boot ROM: one is 4 or 8 KB.`, true );
      continue;
    }
    await take( file.name, bytes );
  }
  event.target.value = "";
} );
$( "kept" ).addEventListener( "change", ( event ) =>
{
  if ( event.target.value )
  {
    putCartridgeIn( event.target.value );
  }
} );

$( "model" ).value = settings.model;
$( "model" ).addEventListener( "change", ( event ) =>
{
  settings.model = event.target.value;
  keepSettings();
  // Another model takes the cartridge out and puts it in again.
  if ( running )
  {
    putCartridgeIn( settings.last );
  }
} );

$( "pause" ).addEventListener( "click", () => setPaused( !paused ) );
$( "power" ).addEventListener( "click", async () =>
{
  const answer = request( "emu.reset" );
  showMessage( answer.ok ? "" : answer.error.message, !answer.ok );
  setPaused( false );
  draw();
  await sync( false );
} );

function stateFile()
{
  return `${ settings.last.slice( CARTRIDGES.length + 1 ).replace( /\.[^.]*$/, "" ) }.wsstate`;
}
$( "save-state" ).addEventListener( "click", async () =>
{
  const answer = request( "state.save", { filename: stateFile() } );
  showMessage( answer.ok ? "State saved." : answer.error.message, !answer.ok );
  await sync( false );
  showFiles();
} );
$( "load-state" ).addEventListener( "click", () =>
{
  const answer = request( "state.load", { filename: stateFile() } );
  showMessage( answer.ok ? "State loaded." : answer.error.message, !answer.ok );
  draw();
} );

$( "upright" ).checked = settings.upright;
$( "upright" ).addEventListener( "change", ( event ) =>
{
  settings.upright = event.target.checked;
  keepSettings();
  draw();
} );
$( "whole" ).checked = settings.whole;
$( "whole" ).addEventListener( "change", ( event ) =>
{
  settings.whole = event.target.checked;
  keepSettings();
  fit();
} );
$( "volume" ).value = String( settings.volume );
$( "volume" ).addEventListener( "input", ( event ) =>
{
  settings.volume = Number( event.target.value );
  sound.setVolume( settings.volume );
  keepSettings();
} );
$( "reset-keys" ).addEventListener( "click", () =>
{
  input.reset();
  showKeys();
} );

$( "send" ).addEventListener( "click", () =>
{
  const answer = ws.handle( $( "request" ).value );
  try
  {
    $( "answer" ).textContent = JSON.stringify( JSON.parse( answer ), null, 2 );
  }
  catch
  {
    $( "answer" ).textContent = answer;
  }
  draw();
} );

const stage = $( "stage" );
stage.addEventListener( "dragover", ( event ) =>
{
  event.preventDefault();
  stage.classList.add( "dropping" );
} );
stage.addEventListener( "dragleave", () => stage.classList.remove( "dropping" ) );
stage.addEventListener( "drop", ( event ) =>
{
  event.preventDefault();
  stage.classList.remove( "dropping" );
  takeFiles( event.dataTransfer.files );
} );

showKeys();
showFiles();
showStatus();

// A cartridge named in the address, ?rom=URL, is fetched and put in; else the
// one put in last, if it is still kept.
const romUrl = new URLSearchParams( location.search ).get( "rom" );
if ( romUrl )
{
  try
  {
    const response = await fetch( romUrl );
    if ( !response.ok )
    {
      throw new Error( `${ response.status } ${ response.statusText }` );
    }
    const name = decodeURIComponent( new URL( romUrl, location.href ).pathname.split( "/" ).pop() );
    await take( name, new Uint8Array( await response.arrayBuffer() ) );
  }
  catch ( error )
  {
    showMessage( `Cannot fetch ${ romUrl }: ${ error.message }`, true );
  }
}
else if ( settings.last && filesIn( CARTRIDGES ).includes( settings.last.slice( CARTRIDGES.length + 1 ) ) )
{
  await putCartridgeIn( settings.last );
}
