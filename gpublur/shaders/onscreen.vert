#version 140

// The quad covering the blurred background, generated from gl_VertexID. What of it shows is
// decided by scissoring to the blur region.
uniform mat4 projection; // viewport projection, device pixels
uniform vec4 rect; // the blurred background, device pixels

out vec2 uv;
out vec2 position;

const vec2 corners[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main()
{
    vec2 corner = corners[gl_VertexID];
    uv = vec2(corner.x, 1.0 - corner.y);
    position = rect.xy + corner * rect.zw;
    gl_Position = projection * vec4(position, 0.0, 1.0);
}
