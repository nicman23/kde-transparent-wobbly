#version 140

#include "sdf.glsl"

uniform sampler2D source;
uniform mat4 colorMatrix; // saturation
uniform float noise; // strength, 0 is off
uniform float noiseScale; // grain size, device px
uniform vec4 box; // window frame: center, half extents, device pixels
uniform vec4 cornerRadius;
uniform float opacity;

in vec2 uv;
in vec2 position;

out vec4 fragColor;

// static per grain, like the blur effect's random noise texture, without uploading one
float hash(vec2 p)
{
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract((p.x + p.y) * p.x);
}

void main()
{
    fragColor = texture(source, uv) * colorMatrix * opacity;
    // additive noise masks banding in the smooth gradients, as in the blur effect
    fragColor.rgb += floor(hash(floor(gl_FragCoord.xy / noiseScale)) * noise * 255.0) / 255.0 * opacity;
    if (cornerRadius != vec4(0.0)) {
        float f = sdfRoundedBox(position, box.xy, box.zw, cornerRadius);
        fragColor *= 1.0 - clamp(0.5 + f / fwidth(f), 0.0, 1.0);
    }
}
