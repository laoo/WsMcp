// What every screen shader is given. SDL_GPU binds a fragment shader's
// sampled textures in set 2 and its uniform buffers in set 3.

layout( set = 2, binding = 0 ) uniform sampler2D source;

layout( set = 3, binding = 0 ) uniform Parameters
{
  // The emulated screen's size in pixels, and its reciprocal.
  vec4 sourceSize;
  // The size drawn to, in pixels of the target, and its reciprocal.
  vec4 outputSize;
  // The preset's own settings, each 0 to 1: x is how dark the LCD's grid is.
  vec4 settings;
};

layout( location = 0 ) in vec2 texCoord;
layout( location = 0 ) out vec4 colour;

// Where to sample so that a linear sampler blends only at the edges of the
// source's pixels: each is drawn as a block of its colour, with a seam one
// target pixel wide, whatever the scale.
vec2 sharpCoord( vec2 coord )
{
  vec2 texel = coord * sourceSize.xy;
  vec2 scale = max( outputSize.xy * sourceSize.zw, vec2( 1.0 ) );
  vec2 offset = fract( texel ) - 0.5;
  vec2 edge = 0.5 - 0.5 / scale;
  vec2 blended = ( offset - clamp( offset, -edge, edge ) ) * scale + 0.5;
  return ( floor( texel ) + blended ) * sourceSize.zw;
}
