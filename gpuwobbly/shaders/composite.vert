#version 140
precision highp int;

// Generates the window mesh from gl_VertexID and bends it along the bicubic Bezier patch whose
// 16 control points live in the physics state texture.
uniform sampler2D state;
uniform mat4 projection; // viewport projection, device pixels
uniform float scale; // logical to device pixels
uniform vec4 frameRect; // logical
uniform vec4 expandedRect; // logical, what the window texture holds
uniform vec4 backgroundRect; // logical, what the blurred background holds
uniform vec2 grid; // tessellation
uniform float maxOffset;

out vec2 texcoord;
out vec2 backgroundTexcoord;
out vec2 frameCoord;

const ivec2 corners[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 0), ivec2(1, 1), ivec2(0, 1));

vec4 bernstein(float t)
{
    float s = 1.0 - t;
    return vec4(s * s * s, 3.0 * s * s * t, 3.0 * s * t * t, t * t * t);
}

void main()
{
    ivec2 cells = ivec2(grid);
    int quad = gl_VertexID / 6;
    ivec2 vertex = ivec2(quad % cells.x, quad / cells.x) + corners[gl_VertexID % 6];
    vec2 t = vec2(vertex) / grid;

    // Bernstein polynomials reproduce the rest grid exactly, so only the offsets need blending.
    vec2 local = expandedRect.xy + t * expandedRect.zw - frameRect.xy;
    vec2 uv = local / frameRect.zw;
    vec4 bx = bernstein(uv.x);
    vec4 by = bernstein(uv.y);
    vec2 displacement = vec2(0.0);
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            displacement += bx[i] * by[j] * texelFetch(state, ivec2(i, j), 0).xy;
        }
    }
    // the shadow lies outside the patch where it can overshoot, keep it inside the repaint bound
    displacement = clamp(displacement, -maxOffset, maxOffset);
    vec2 position = frameRect.xy + local + displacement;

    texcoord = vec2(t.x, 1.0 - t.y);
    vec2 b = (position - backgroundRect.xy) / backgroundRect.zw;
    backgroundTexcoord = vec2(b.x, 1.0 - b.y);
    frameCoord = uv;
    gl_Position = projection * vec4(position * scale, 0.0, 1.0);
}
