#version 450
// A triangle that covers the target, and the texture coordinate at each
// corner, from the vertex index alone: no vertex buffer is bound.

layout( location = 0 ) out vec2 texCoord;

void main()
{
  vec2 corner = vec2( ( gl_VertexIndex << 1 ) & 2, gl_VertexIndex & 2 );
  texCoord = corner;
  gl_Position = vec4( corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0 );
}
