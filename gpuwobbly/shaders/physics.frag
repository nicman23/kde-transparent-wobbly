#version 140
precision highp int;

// The wobbly spring grid, advanced on the GPU. The state is a 5x4 texture: texel (i, j) with
// i < 4 holds grid point (i, j) as offset.xy (from its rest position on the window) and
// velocity.xy; texel (4, 0) receives the energy (acceleration sum, velocity sum) of the last step.
// The grid is tiny, so every fragment simulates all 16 points and keeps only its own: one pass
// per frame instead of a pass per substep.

uniform sampler2D state;
uniform vec4 oldRect; // frame geometry the state is relative to
uniform vec4 newRect; // frame geometry now
uniform int constraints; // bit k: point k is pinned to its rest position
uniform int edges; // bits 1 top, 2 bottom, 4 left, 8 right: edge may wobble
uniform int reset;
uniform int steps;
uniform float dt;
uniform float lastDt;
uniform float stiffness;
uniform float drag;
uniform float moveFactor;
uniform vec2 velocityBounds; // min, max
uniform vec2 accelerationBounds; // min, max
uniform float maxOffset;

out vec4 fragColor;

vec2 offsets[16];
vec2 velocities[16];
vec2 accelerations[16];
vec2 scratch[16];

vec2 fixBounds(vec2 v, vec2 bounds)
{
    return mix(clamp(v, -bounds.y, bounds.y), vec2(0.0), lessThan(abs(v), vec2(bounds.x)));
}

// Weighted mean with the 8-neighbourhood, the point itself counts as much as all its neighbours.
void neighbourMean(inout vec2 data[16])
{
    for (int k = 0; k < 16; ++k) {
        int i = k % 4;
        int j = k / 4;
        vec2 sum = vec2(0.0);
        float n = 0.0;
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                int x = i + di;
                int y = j + dj;
                if ((di != 0 || dj != 0) && x >= 0 && x < 4 && y >= 0 && y < 4) {
                    sum += data[y * 4 + x];
                    n += 1.0;
                }
            }
        }
        scratch[k] = (sum + n * data[k]) / (2.0 * n);
    }
    for (int k = 0; k < 16; ++k) {
        data[k] = scratch[k];
    }
}

vec2 advance(float t)
{
    // Springs to the 4-neighbourhood. In offset space the rest length cancels out:
    // (neighbour - pos) - restLength == neighbourOffset - offset.
    for (int k = 0; k < 16; ++k) {
        if ((constraints & (1 << k)) != 0) {
            accelerations[k] = -offsets[k] * stiffness;
            continue;
        }
        int i = k % 4;
        int j = k / 4;
        vec2 sum = vec2(0.0);
        float n = 0.0;
        if (i > 0) { sum += offsets[k - 1] - offsets[k]; n += 1.0; }
        if (i < 3) { sum += offsets[k + 1] - offsets[k]; n += 1.0; }
        if (j > 0) { sum += offsets[k - 4] - offsets[k]; n += 1.0; }
        if (j < 3) { sum += offsets[k + 4] - offsets[k]; n += 1.0; }
        accelerations[k] = sum * stiffness / n;
    }
    neighbourMean(accelerations);

    float accelerationSum = 0.0;
    for (int k = 0; k < 16; ++k) {
        vec2 a = fixBounds(accelerations[k], accelerationBounds);
        velocities[k] = a * t + velocities[k] * drag;
        accelerationSum += abs(a.x) + abs(a.y);
    }
    neighbourMean(velocities);

    float velocitySum = 0.0;
    for (int k = 0; k < 16; ++k) {
        int i = k % 4;
        int j = k / 4;
        velocities[k] = fixBounds(velocities[k], velocityBounds);
        offsets[k] += velocities[k] * t * moveFactor;
        velocitySum += abs(velocities[k].x) + abs(velocities[k].y);

        // on a resize, sides that have not moved yet stay put
        if ((edges & 1) == 0 && j < 3) offsets[k].y = 0.0;
        if ((edges & 2) == 0 && j > 0) offsets[k].y = 0.0;
        if ((edges & 4) == 0 && i < 3) offsets[k].x = 0.0;
        if ((edges & 8) == 0 && i > 0) offsets[k].x = 0.0;

        // keeps the deformed window inside a bound the CPU knows without reading anything back
        offsets[k] = clamp(offsets[k], -maxOffset, maxOffset);
    }
    return vec2(accelerationSum, velocitySum);
}

void main()
{
    for (int k = 0; k < 16; ++k) {
        vec4 s = reset != 0 ? vec4(0.0) : texelFetch(state, ivec2(k % 4, k / 4), 0);
        vec2 cell = vec2(float(k % 4), float(k / 4)) / 3.0;
        // The window moved or resized: points keep their absolute position, so their offsets shift.
        offsets[k] = s.xy - ((newRect.xy + cell * newRect.zw) - (oldRect.xy + cell * oldRect.zw));
        velocities[k] = s.zw;
    }

    vec2 energy = vec2(0.0);
    for (int n = 0; n < steps; ++n) {
        energy = advance(n == steps - 1 ? lastDt : dt);
    }

    ivec2 p = ivec2(gl_FragCoord.xy);
    if (p.x == 4) {
        fragColor = vec4(energy, 0.0, 0.0);
    } else {
        int k = p.y * 4 + p.x;
        fragColor = vec4(offsets[k], velocities[k]);
    }
}
