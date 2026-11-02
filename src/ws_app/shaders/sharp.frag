#version 450
#extension GL_GOOGLE_include_directive : require
// Each pixel a block of its colour, the seams between them blended a target
// pixel wide: square and even at any scale, and exact at a whole one.

#include "common.glsl"

void main()
{
  colour = vec4( texture( source, sharpCoord( texCoord ) ).rgb, 1.0 );
}
