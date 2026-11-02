#version 450
#extension GL_GOOGLE_include_directive : require
// The LCD's dots as blocks of their colour with dark gaps between them, about
// a target pixel wide, as a WonderSwan's screen shows them up close:
// settings.x is how dark. Below three target pixels a dot there is no room
// for a gap, and the picture is drawn sharp.

#include "common.glsl"

void main()
{
  vec3 pixel = texture( source, sharpCoord( texCoord ) ).rgb;
  vec2 texel = texCoord * sourceSize.xy;
  vec2 scale = outputSize.xy * sourceSize.zw;
  // How far, in target pixels, this one is from its dot's nearest edge.
  vec2 within = fract( texel );
  vec2 fromEdge = min( within, 1.0 - within ) * scale;
  float gap = 1.0 - smoothstep( 0.0, 1.0, min( fromEdge.x, fromEdge.y ) );
  float room = step( 3.0, min( scale.x, scale.y ) );
  colour = vec4( pixel * ( 1.0 - settings.x * gap * room ), 1.0 );
}
